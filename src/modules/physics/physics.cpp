#include "augusta/physics.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// The headers this file uses, not PxPhysicsAPI.h: that one also pulls in
// PxVecMath.h, whose Windows SIMD path reads cl's __m128 union members
// (m128_u16), which clang-cl's __m128 doesn't have.
#include <PxMaterial.h>
#include <PxPhysics.h>
#include <PxQueryFiltering.h>
#include <PxQueryReport.h>
#include <PxRigidDynamic.h>
#include <PxRigidStatic.h>
#include <PxScene.h>
#include <PxSceneDesc.h>
#include <characterkinematic/PxCapsuleController.h>
#include <characterkinematic/PxControllerManager.h>
#include <common/PxTolerancesScale.h>
#include <cooking/PxCooking.h>
#include <extensions/PxDefaultAllocator.h>
#include <extensions/PxDefaultCpuDispatcher.h>
#include <extensions/PxDefaultSimulationFilterShader.h>
#include <extensions/PxRigidActorExt.h>
#include <foundation/PxErrorCallback.h>
#include <foundation/PxFoundation.h>
#include <foundation/PxPhysicsVersion.h>
#include <geometry/PxCapsuleGeometry.h>
#include <geometry/PxTriangleMesh.h>
#include <geometry/PxTriangleMeshGeometry.h>
#if AUGUSTA_DEBUG_TOOLS
#include <chrono>
#include <filesystem>
#include <format>
#include <string>

#include <PxVisualizationParameter.h>
#include <common/PxRenderBuffer.h>
#include <foundation/PxBounds3.h>
#include <omnipvd/PxOmniPvd.h>
#include <pvdruntime/OmniPvdFileWriteStream.h>
#include <pvdruntime/OmniPvdWriter.h>
#endif

#include "augusta/grid.h"
#include "augusta/logging.h"
#include "augusta/math.h"

// M1 spike (ADR-0002): the first real (non-stub) body for this module,
// backed by PhysX's character controller (CCT) rather than a raw rigid
// dynamic - a good fit for "general body movement, stance transitions"
// (physics.h's header comment), since CCT natively supports capsule
// resize (crouch/prone) and sweep-based move() without the caller having
// to hand-roll collision response.
//
// A World moves its bodies with PxController::move() alone. CCT movement is
// sweep-based and self-contained; nothing here needs the rigid-body dynamics
// loop, since every body is player-controlled and the only other geometry is
// the map's collision meshes (AddCollisionMesh), which never move. The one
// PxScene::simulate()/fetchResults() a World runs is its visualization's step
// (StepVisualization, Development build only), which moves nothing a body's
// movement sees.
//
// Engine convention (not yet pinned down project-wide - see
// augusta::command::Command's yaw/pitch comment): Y is up, matching both
// GLM's and PhysX's own default.
namespace augusta::physics {

namespace {

// Individual using-declarations rather than `using namespace physx` -
// Google style (ADR-0012) forbids using-directives.
using physx::PxCapsuleControllerDesc;
using physx::PxCapsuleGeometry;
using physx::PxController;
using physx::PxControllerCollisionFlag;
using physx::PxControllerCollisionFlags;
using physx::PxControllerFilters;
using physx::PxControllerManager;
using physx::PxCookingParams;
using physx::PxDefaultAllocator;
using physx::PxDefaultCpuDispatcher;
using physx::PxDefaultCpuDispatcherCreate;
using physx::PxDefaultSimulationFilterShader;
using physx::PxErrorCallback;
using physx::PxErrorCode;
using physx::PxExtendedVec3;
using physx::PxFoundation;
using physx::PxHitFlag;
using physx::PxIdentity;
using physx::PxMaterial;
using physx::PxOmniPvd;
using physx::PxOverlapBuffer;
using physx::PxPhysics;
using physx::PxQuat;
using physx::PxQueryFilterData;
using physx::PxQueryFlag;
using physx::PxRaycastBuffer;
using physx::PxRigidActor;
using physx::PxRigidActorExt;
using physx::PxRigidStatic;
using physx::PxScene;
using physx::PxSceneDesc;
using physx::PxTolerancesScale;
using physx::PxTransform;
using physx::PxTriangleMesh;
using physx::PxTriangleMeshDesc;
using physx::PxTriangleMeshGeometry;
using physx::PxU32;
using physx::PxVec3;

// ---- Tuning constants ----
// Spike placeholders (ADR-0002 doesn't pin these down, and unlike
// StaminaConfig these aren't yet threaded through as data): move to
// external configuration once gameplay tuning needs it, same posture as
// StaminaConfig's own header comment.
constexpr float kGravity = -9.81F;  // m/s^2, along -Y.
constexpr float kCapsuleRadius = 0.3F;
constexpr float kStandingHeight = 1.5F;  // Capsule cylinder height, excludes hemispherical caps.
constexpr float kCrouchingHeight = 0.7F;
constexpr float kProneHeight = 0.1F;
// The capsule's two hemispherical caps together add its diameter to its height.
constexpr float kCapsuleDiameter = 2.0F * kCapsuleRadius;
constexpr float kStepOffset = 0.3F;
constexpr float kWalkSpeed = 3.0F;  // m/s, standing baseline.
constexpr float kSprintMultiplier = 1.6F;
constexpr float kCrouchSpeedMultiplier = 0.6F;
constexpr float kProneSpeedMultiplier = 0.3F;
constexpr float kStaticFriction = 0.5F;
constexpr float kDynamicFriction = 0.5F;
constexpr float kRestitution = 0.1F;
constexpr float kMinMoveDistance = 0.001F;  // PxController::move's own minDist parameter.
// PxSceneDesc rejects a scene without a CPU dispatcher, but the only
// PxScene::simulate() here is the visualization's (see the header comment),
// which a dispatcher without threads runs on the calling thread.
constexpr int kWorkerThreadCount = 0;

// A stance change that grows the capsule is tested for headroom with a capsule
// shrunk by this much, so touching the floor or a wall is not "overlapping".
constexpr float kStanceCheckSkin = 0.02F;

float HeightForStance(Stance stance) {
  switch (stance) {
    case Stance::kStanding:
      return kStandingHeight;
    case Stance::kCrouching:
      return kCrouchingHeight;
    case Stance::kProne:
      return kProneHeight;
  }
  return kStandingHeight;
}

float SpeedMultiplierForStance(Stance stance) {
  switch (stance) {
    case Stance::kStanding:
      return 1.0F;
    case Stance::kCrouching:
      return kCrouchSpeedMultiplier;
    case Stance::kProne:
      return kProneSpeedMultiplier;
  }
  return 1.0F;
}

// Decision half of Step's stamina rule (US-05): given this tick's sprint
// request, whether the body is standing and moving, and its stamina and
// exhaustion, resolves whether sprint is actually honored and the resulting
// stamina and exhaustion. Pure - no PxController calls - so it stays
// unit-testable independent of PhysX; Step (mechanism half) just applies the
// result.
struct StaminaResult {
  bool sprinting = false;
  float stamina = 1.0F;
  bool exhausted = false;
};

// Sprint is honored only standing, moving and not exhausted, and only then
// drains; otherwise stamina regenerates. Running stamina out exhausts the
// body, and it stays exhausted, walking, until stamina is back above
// forced_walk_below, so a body held at the threshold does not flicker between
// sprinting and walking.
StaminaResult ResolveStamina(bool sprint_requested, bool standing_and_moving, float current_stamina,
                             bool currently_exhausted, const StaminaConfig& config, float delta_time) {
  StaminaResult result;
  result.sprinting = sprint_requested && standing_and_moving && !currently_exhausted;
  if (result.sprinting) {
    result.stamina = std::max(0.0F, current_stamina - (config.deplete_per_second * delta_time));
  } else {
    result.stamina = std::min(1.0F, current_stamina + (config.regen_per_second * delta_time));
  }
  if (currently_exhausted) {
    result.exhausted = result.stamina <= config.forced_walk_below;
  } else {
    result.exhausted = result.stamina <= 0.0F;
  }
  return result;
}

// Decision half of Step's move speed: this tick's horizontal speed given
// stance and the effective (post-ResolveStamina) sprint state. Pure, same
// testability rationale as ResolveStamina above.
float ResolveSpeed(Stance stance, bool sprinting) {
  float speed = kWalkSpeed * SpeedMultiplierForStance(stance);
  if (sprinting && stance == Stance::kStanding) {
    speed *= kSprintMultiplier;
  }
  return speed;
}

// Every triangle twice, once per winding: a cooked map's triangles can face
// either way, and the character controller only collides with the side a
// triangle faces (PxMeshGeometryFlag::eDOUBLE_SIDED does not change that), so a
// body would otherwise fall through a floor or walk through a wall authored the
// "wrong" way round.
std::vector<std::uint32_t> WithBothWindings(const std::vector<std::uint32_t>& indices) {
  std::vector<std::uint32_t> both = indices;
  both.reserve(indices.size() * 2);
  for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
    both.insert(both.end(), {indices[i], indices[i + 2], indices[i + 1]});
  }
  return both;
}

PxVec3 ToPx(const math::Vec3& vec) { return {vec.x, vec.y, vec.z}; }
math::Vec3 FromPx(const PxVec3& vec) { return {vec.x, vec.y, vec.z}; }
math::Vec3 FromPx(const PxExtendedVec3& vec) {
  return {static_cast<float>(vec.x), static_cast<float>(vec.y), static_cast<float>(vec.z)};
}

// Routes PhysX's own diagnostics through this engine's logging (ADR-0029)
// instead of PhysX's default stderr output.
class LogErrorCallback : public PxErrorCallback {
 public:
  // NOLINTNEXTLINE(readability-identifier-naming) - name mandated by PxErrorCallback's own virtual signature.
  void reportError(PxErrorCode::Enum code, const char* message, const char* file, int line) override {
    LOG_ERROR("subsystem=physics event=physx_error code={} message=\"{}\" file={} line={}", static_cast<int>(code),
              message, file, line);
  }
};

// PhysX allows one PxFoundation per process, and creating PxPhysics on it is
// costly, so every World shares one of each, created by the first World and
// released with the last - a server and several clients in one test process
// each own a World.
struct PhysxProcessState {
  std::mutex mutex;
  int users = 0;
  // Must outlive the foundation, so they live here rather than in each World.
  LogErrorCallback error_callback;
  PxDefaultAllocator allocator;
  PxFoundation* foundation = nullptr;
#if AUGUSTA_DEBUG_TOOLS
  // What samples physics into a recording (StartRecording); null when none does.
  PxOmniPvd* omni_pvd = nullptr;
#endif
  PxPhysics* physics = nullptr;
};

PhysxProcessState& GlobalState() {
  static PhysxProcessState state;
  return state;
}

struct PhysxHandles {
  PxFoundation* foundation;
  PxPhysics* physics;
};

void CreateFoundation(PhysxProcessState& state) {
  state.foundation = PxCreateFoundation(PX_PHYSICS_VERSION, state.allocator, state.error_callback);
  if (state.foundation == nullptr) {
    throw std::runtime_error("physics::World: PxCreateFoundation failed");
  }
}

// Releases what the state holds, in the reverse of the order it was made in:
// physics before what samples it, and the foundation last.
void ReleaseProcessState(PhysxProcessState& state) {
  if (state.physics != nullptr) {
    state.physics->release();
    state.physics = nullptr;
  }
#if AUGUSTA_DEBUG_TOOLS
  if (state.omni_pvd != nullptr) {
    state.omni_pvd->release();
    state.omni_pvd = nullptr;
  }
#endif
  if (state.foundation != nullptr) {
    state.foundation->release();
    state.foundation = nullptr;
  }
}

// Creates physics on the state's foundation, sampled by its omni_pvd if it has one.
void CreatePhysics(PhysxProcessState& state) {
  const PxTolerancesScale scale;
#if AUGUSTA_DEBUG_TOOLS
  state.physics = PxCreatePhysics(PX_PHYSICS_VERSION, *state.foundation, scale, true, nullptr, state.omni_pvd);
#else
  state.physics = PxCreatePhysics(PX_PHYSICS_VERSION, *state.foundation, scale, true, nullptr);
#endif
  if (state.physics == nullptr) {
    ReleaseProcessState(state);
    throw std::runtime_error("physics::World: PxCreatePhysics failed");
  }
}

PhysxHandles AcquirePhysx() {
  PhysxProcessState& state = GlobalState();
  const std::lock_guard<std::mutex> lock(state.mutex);
  if (state.users == 0) {
    CreateFoundation(state);
    CreatePhysics(state);
  }
  ++state.users;
  return {.foundation = state.foundation, .physics = state.physics};
}

void ReleasePhysx() {
  PhysxProcessState& state = GlobalState();
  const std::lock_guard<std::mutex> lock(state.mutex);
  if (--state.users == 0) {
    ReleaseProcessState(state);
  }
}

// Holds one use of the shared PhysX state for as long as it lives. Being a
// member, it is released even if the owning World's constructor throws after
// it was acquired, when the World's destructor would not run.
class PhysxLease {
 public:
  PhysxLease() : handles_(AcquirePhysx()) {}
  // Takes over a use already counted (StartRecording's).
  explicit PhysxLease(PhysxHandles counted) : handles_(counted) {}
  ~PhysxLease() { ReleasePhysx(); }
  PhysxLease(const PhysxLease&) = delete;
  PhysxLease& operator=(const PhysxLease&) = delete;

  [[nodiscard]] PxPhysics* Physics() const { return handles_.physics; }

 private:
  PhysxHandles handles_;
};

// state on the grids the Networking Protocol sends a body on (augusta/grid.h, ADR-0038).
BodyState OnWireGrid(BodyState state) {
  state.position = math::SnapPosition(state.position);
  state.velocity = math::SnapVelocity(state.velocity);
  state.stamina = math::SnapStamina(state.stamina);
  return state;
}

// stride advanced by moved's distance along the ground over stride_length,
// rounded to a whole step and round the cycle; held without a stride length.
std::uint8_t AdvanceStride(std::uint8_t stride, const math::Vec3& moved, float stride_length) {
  if (!(stride_length > 0.0F)) {
    return stride;
  }
  const float ground = math::Length(math::Vec3(moved.x, 0.0F, moved.z));
  const std::int64_t steps = std::llround(ground / stride_length * static_cast<float>(kStrideSteps));
  return static_cast<std::uint8_t>((stride + steps) % kStrideSteps);
}

PxExtendedVec3 ToFootPosition(const math::Vec3& position) { return {position.x, position.y, position.z}; }

}  // namespace

float StanceHeight(Stance stance) { return HeightForStance(stance) + kCapsuleDiameter; }

math::Vec3 LowerToStance(const math::Vec3& standing_point, Stance stance) {
  const float scale = StanceHeight(stance) / StanceHeight(Stance::kStanding);
  return {standing_point.x, standing_point.y * scale, standing_point.z};
}

Capsule CapsuleOf(const BodyState& state) {
  const math::Vec3 bottom = state.position + math::Vec3(0.0F, kCapsuleRadius, 0.0F);
  return {.bottom = bottom,
          .top = bottom + math::Vec3(0.0F, HeightForStance(state.stance), 0.0F),
          .radius = kCapsuleRadius};
}

// Per-body bookkeeping PhysX's controller doesn't itself track: a CCT has
// no notion of "velocity" the way a rigid dynamic does, so World derives
// and caches it each Step from positional delta / delta_time; stance and
// stamina are gameplay state PhysX knows nothing about either.
struct BodyRecord {
  PxController* controller = nullptr;
  BodyState state;
  // Accumulated vertical speed for the simple constant-acceleration
  // gravity integration in Step - reset to 0 whenever the controller was
  // grounded as of the previous Step.
  float vertical_speed = 0.0F;
  bool grounded = false;
};

struct World::Impl {
  // Declared first so it is released last: everything below that PhysX
  // created must be released before the shared foundation can go.
  PhysxLease lease;
  PxPhysics* physics = lease.Physics();
  PxDefaultCpuDispatcher* dispatcher = nullptr;
  PxScene* scene = nullptr;
  PxControllerManager* controller_manager = nullptr;
  PxMaterial* material = nullptr;
  // Static map geometry added by AddCollisionMesh; released with the World.
  std::vector<PxTriangleMesh*> collision_meshes;
  std::vector<PxRigidStatic*> static_actors;
  // The Surface id each of static_actors was added with.
  std::unordered_map<const PxRigidActor*, SurfaceId> surfaces;
  StaminaConfig stamina_config;
  std::unordered_map<BodyHandle, BodyRecord> bodies;
  std::uint32_t next_handle = 1;
#if AUGUSTA_DEBUG_TOOLS
  // What SetVisualization last applied to PhysX, whose scene starts with its
  // visualization off.
  Visualization visualization;
#endif

  explicit Impl(const StaminaConfig& config) : stamina_config(config) {
    PxSceneDesc scene_desc(physics->getTolerancesScale());
    scene_desc.gravity = PxVec3(0.0F, kGravity, 0.0F);
    dispatcher = PxDefaultCpuDispatcherCreate(kWorkerThreadCount);
    scene_desc.cpuDispatcher = dispatcher;
    scene_desc.filterShader = PxDefaultSimulationFilterShader;

    scene = physics->createScene(scene_desc);
    controller_manager = PxCreateControllerManager(*scene);
    material = physics->createMaterial(kStaticFriction, kDynamicFriction, kRestitution);
    LOG_DEBUG("subsystem=physics event=world_created");
  }

  // Decision half of a stance change: a smaller capsule always fits, a taller
  // one only if it does not overlap static geometry (e.g. standing up under a
  // low ceiling). A static-only scene query never sees the body's own
  // controller, which is dynamic.
  [[nodiscard]] bool CanChangeStance(const BodyRecord& record, Stance current, Stance target) const {
    if (HeightForStance(target) <= HeightForStance(current)) {
      return true;
    }
    const float height = HeightForStance(target);
    const PxExtendedVec3 foot = record.controller->getFootPosition();
    const float center_y = static_cast<float>(foot.y) + kCapsuleRadius + (height * 0.5F);
    // A PxCapsuleGeometry lies along x; the rotation stands it up along y.
    const PxTransform pose(PxVec3(static_cast<float>(foot.x), center_y, static_cast<float>(foot.z)),
                           PxQuat(physx::PxHalfPi, PxVec3(0.0F, 0.0F, 1.0F)));
    const PxCapsuleGeometry capsule(kCapsuleRadius - kStanceCheckSkin, height * 0.5F);
    PxOverlapBuffer hit;
    const PxQueryFilterData filter(PxQueryFlag::eSTATIC | PxQueryFlag::eANY_HIT);
    return !scene->overlap(capsule, pose, hit, filter);
  }

  ~Impl() {
    for (auto& [handle, record] : bodies) {
      if (record.controller != nullptr) {
        record.controller->release();
      }
    }
    for (PxRigidStatic* actor : static_actors) {
      actor->release();
    }
    for (PxTriangleMesh* mesh : collision_meshes) {
      mesh->release();
    }
    if (material != nullptr) {
      material->release();
    }
    if (controller_manager != nullptr) {
      controller_manager->release();
    }
    if (scene != nullptr) {
      scene->release();
    }
    if (dispatcher != nullptr) {
      dispatcher->release();
    }
  }
};

World::World(const StaminaConfig& config) : impl_(std::make_unique<Impl>(config)) {}
World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

std::string_view DescribeCollisionMeshError(CollisionMeshError error) {
  switch (error) {
    case CollisionMeshError::kEmpty:
      return "the mesh has no triangles";
    case CollisionMeshError::kInvalidIndex:
      return "the mesh's indices are not whole triangles inside its points";
    case CollisionMeshError::kCookingFailed:
      return "PhysX could not build a collision mesh from it";
  }
  return "unknown collision mesh error";
}

std::expected<void, CollisionMeshError> ValidateCollisionMesh(const CollisionMesh& mesh) {
  if (mesh.points.empty() || mesh.indices.empty()) {
    return std::unexpected(CollisionMeshError::kEmpty);
  }
  const bool whole_triangles = mesh.indices.size() % 3 == 0;
  const bool in_range =
      std::ranges::all_of(mesh.indices, [&](std::uint32_t index) { return index < mesh.points.size(); });
  if (!whole_triangles || !in_range) {
    return std::unexpected(CollisionMeshError::kInvalidIndex);
  }
  return {};
}

std::expected<void, CollisionMeshError> World::AddCollisionMesh(const CollisionMesh& mesh, SurfaceId surface) {
  if (const auto valid = ValidateCollisionMesh(mesh); !valid) {
    return valid;
  }
  const std::vector<std::uint32_t> both_windings = WithBothWindings(mesh.indices);

  PxTriangleMeshDesc desc;
  desc.points.count = static_cast<PxU32>(mesh.points.size());
  desc.points.stride = sizeof(math::Vec3);
  desc.points.data = mesh.points.data();
  desc.triangles.count = static_cast<PxU32>(both_windings.size() / 3);
  desc.triangles.stride = 3 * sizeof(std::uint32_t);
  desc.triangles.data = both_windings.data();

  const PxCookingParams params(impl_->physics->getTolerancesScale());
  PxTriangleMesh* cooked = PxCreateTriangleMesh(params, desc, impl_->physics->getPhysicsInsertionCallback());
  if (cooked == nullptr) {
    return std::unexpected(CollisionMeshError::kCookingFailed);
  }
  PxRigidStatic* actor = impl_->physics->createRigidStatic(PxTransform(PxIdentity));
  PxRigidActorExt::createExclusiveShape(*actor, PxTriangleMeshGeometry(cooked), *impl_->material);
  impl_->scene->addActor(*actor);
  impl_->collision_meshes.push_back(cooked);
  impl_->static_actors.push_back(actor);
  impl_->surfaces.emplace(actor, surface);
  LOG_DEBUG("subsystem=physics event=collision_mesh_added triangles={}", mesh.indices.size() / 3);
  return {};
}

void World::SetStaminaConfig(const StaminaConfig& config) { impl_->stamina_config = config; }

BodyHandle World::CreateBody(const math::Vec3& initial_position) {
  PxCapsuleControllerDesc desc;
  desc.radius = kCapsuleRadius;
  desc.height = kStandingHeight;
  desc.material = impl_->material;
  desc.stepOffset = kStepOffset;
  desc.upDirection = PxVec3(0.0F, 1.0F, 0.0F);
  PxController* controller = impl_->controller_manager->createController(desc);
  if (controller == nullptr) {
    throw std::runtime_error("physics::World::CreateBody: createController failed");
  }
  // The descriptor's position is the capsule's center; BodyState's is the feet.
  const math::Vec3 position = math::SnapPosition(initial_position);
  controller->setFootPosition(ToFootPosition(position));

  const auto handle = static_cast<BodyHandle>(impl_->next_handle++);
  BodyRecord record;
  record.controller = controller;
  record.state.position = position;
  impl_->bodies.emplace(handle, std::move(record));
  LOG_DEBUG("subsystem=physics event=body_created handle={}", static_cast<std::uint32_t>(handle));
  return handle;
}

void World::DestroyBody(BodyHandle handle) {
  const auto body_it = impl_->bodies.find(handle);
  if (body_it == impl_->bodies.end()) {
    return;
  }
  body_it->second.controller->release();
  impl_->bodies.erase(body_it);
  LOG_DEBUG("subsystem=physics event=body_destroyed handle={}", static_cast<std::uint32_t>(handle));
}

BodyState World::Step(BodyHandle handle, const MovementInput& input, float delta_time, float stride_length) {
  const auto body_it = impl_->bodies.find(handle);
  if (body_it == impl_->bodies.end()) {
    return {};
  }
  BodyRecord& record = body_it->second;
  BodyState& state = record.state;

  if (input.desired_stance != state.stance && impl_->CanChangeStance(record, state.stance, input.desired_stance)) {
    record.controller->resize(HeightForStance(input.desired_stance));
    state.stance = input.desired_stance;
  }

  const math::Vec3 direction = math::Normalize(input.direction);
  const bool standing_and_moving = state.stance == Stance::kStanding && math::Length(direction) > 0.0F;
  const StaminaResult stamina = ResolveStamina(input.sprint, standing_and_moving, state.stamina, state.exhausted,
                                               impl_->stamina_config, delta_time);
  state.stamina = stamina.stamina;
  state.exhausted = stamina.exhausted;

  const float speed = ResolveSpeed(state.stance, stamina.sprinting);

  // Simple constant-acceleration gravity: reset the accumulated vertical
  // speed whenever the controller was grounded as of the previous Step,
  // otherwise keep accelerating downward.
  if (record.grounded) {
    record.vertical_speed = 0.0F;
  }
  record.vertical_speed += kGravity * delta_time;

  PxVec3 displacement = ToPx(direction) * speed * delta_time;
  displacement.y = record.vertical_speed * delta_time;

  const PxControllerFilters filters;
  const PxControllerCollisionFlags flags = record.controller->move(displacement, kMinMoveDistance, delta_time, filters);
  record.grounded = flags.isSet(PxControllerCollisionFlag::eCOLLISION_DOWN);

  // The controller is put back on the grid too, so the next Step starts from
  // exactly the position this one reports.
  const math::Vec3 new_position = math::SnapPosition(FromPx(record.controller->getFootPosition()));
  record.controller->setFootPosition(ToFootPosition(new_position));
  state.velocity = delta_time > 0.0F ? (new_position - state.position) / delta_time : math::Vec3{};
  state.stride = AdvanceStride(state.stride, new_position - state.position, stride_length);
  state.position = new_position;
  state = OnWireGrid(state);

  return state;
}

void World::SetState(BodyHandle handle, const BodyState& state) {
  const auto body_it = impl_->bodies.find(handle);
  if (body_it == impl_->bodies.end()) {
    return;
  }
  BodyRecord& record = body_it->second;
  const BodyState on_grid = OnWireGrid(state);
  if (on_grid.stance != record.state.stance) {
    record.controller->resize(HeightForStance(on_grid.stance));
  }
  record.controller->setFootPosition(ToFootPosition(on_grid.position));
  record.state = on_grid;
  record.vertical_speed = 0.0F;
  record.grounded = false;
}

FallState World::Fall(BodyHandle handle) const {
  const auto body_it = impl_->bodies.find(handle);
  if (body_it == impl_->bodies.end()) {
    return {};
  }
  return FallState{.vertical_speed = body_it->second.vertical_speed, .grounded = body_it->second.grounded};
}

BodyState World::Restore(BodyHandle handle, const BodyState& state, const FallState& fall) {
  const BodyState on_grid = OnWireGrid(state);
  const auto body_it = impl_->bodies.find(handle);
  if (body_it == impl_->bodies.end()) {
    return on_grid;
  }
  BodyRecord& record = body_it->second;

  // Applied directly against the controller (not via SetState): SetState
  // resets fall/ground tracking, which is right for an intentional teleport
  // (spawn/respawn) but not for putting the body back where it was.
  if (on_grid.stance != record.state.stance) {
    record.controller->resize(HeightForStance(on_grid.stance));
  }
  record.controller->setFootPosition(ToFootPosition(on_grid.position));
  record.state = on_grid;
  record.vertical_speed = fall.vertical_speed;
  record.grounded = fall.grounded;
  return on_grid;
}

RaycastHit World::Raycast(const math::Vec3& origin, const math::Vec3& direction, float max_distance) const {
  RaycastHit result;
  const float length = math::Length(direction);
  if (length <= 0.0F || max_distance <= 0.0F) {
    return result;
  }
  const math::Vec3 dir = direction / length;

  PxRaycastBuffer hit;
  const bool has_hit = impl_->scene->raycast(ToPx(origin), ToPx(dir), max_distance, hit);
  if (!has_hit || !hit.hasBlock) {
    return result;
  }

  result.has_hit = true;
  result.point = FromPx(hit.block.position);
  result.distance = hit.block.distance;
  // A controller creates its own PxRigidDynamic actor internally
  // (PxController::getActor()), which is what the query reports back -
  // there is no PhysX-side mapping from actor to BodyHandle, so this
  // does a linear scan over this World's (currently few) bodies.
  for (const auto& [handle, record] : impl_->bodies) {
    if (record.controller->getActor() == hit.block.actor) {
      result.body = handle;
      break;
    }
  }
  return result;
}

MapRaycastHit World::RaycastMap(const math::Vec3& origin, const math::Vec3& direction, float max_distance) const {
  MapRaycastHit result;
  const float length = math::Length(direction);
  if (length <= 0.0F || max_distance <= 0.0F) {
    return result;
  }
  const math::Vec3 dir = direction / length;

  // The query admits static actors only, and the Map's meshes are the only
  // static actors (AddCollisionMesh), every controller's being dynamic. So
  // whatever it hits has the Surface id it was added with in surfaces.
  PxRaycastBuffer hit;
  const PxQueryFilterData filter(PxQueryFlag::eSTATIC);
  const bool has_hit = impl_->scene->raycast(ToPx(origin), ToPx(dir), max_distance, hit, PxHitFlag::eDEFAULT, filter);
  if (!has_hit || !hit.hasBlock) {
    return result;
  }
  result.has_hit = true;
  result.point = FromPx(hit.block.position);
  result.distance = hit.block.distance;
  result.surface = impl_->surfaces.at(hit.block.actor);
  return result;
}

#if AUGUSTA_DEBUG_TOOLS
namespace {

// Why PxCreateOmniPvd made nothing. Only a Debug PhysX has OmniPVD, every other
// build compiling it out, and the PhysX package hands a Debug build _DEBUG.
#ifdef _DEBUG
constexpr std::string_view kNoOmniPvd = "OmniPVD's runtime (PVDRuntime_64) did not load from beside the executable";
#else
constexpr std::string_view kNoOmniPvd = "this build's PhysX has no OmniPVD: only a Debug build records";
#endif

// Points the state's new omni_pvd at a file stream writing file, or says why it cannot.
std::expected<void, RecordingError> OpenRecording(PhysxProcessState& state, const std::filesystem::path& file) {
  state.omni_pvd = PxCreateOmniPvd(*state.foundation);
  if (state.omni_pvd == nullptr) {
    return std::unexpected(RecordingError::kUnavailable);
  }
  OmniPvdWriter* writer = state.omni_pvd->getWriter();
  OmniPvdFileWriteStream* stream = state.omni_pvd->getFileWriteStream();
  if (writer == nullptr || stream == nullptr) {
    return std::unexpected(RecordingError::kRefused);
  }
  stream->setFileName(file.string().c_str());
  writer->setWriteStream(*stream);
  return {};
}

// Makes the shared PhysX state recorded into file and counts the recording's
// use of it, or says why it cannot, leaving the state as it was.
std::expected<PhysxHandles, RecordingError> AcquireRecordedPhysx(const std::filesystem::path& file) {
  PhysxProcessState& state = GlobalState();
  const std::lock_guard<std::mutex> lock(state.mutex);
  if (state.users != 0) {
    return std::unexpected(RecordingError::kWorldExists);
  }
  CreateFoundation(state);
  if (auto opened = OpenRecording(state, file); !opened) {
    ReleaseProcessState(state);
    return std::unexpected(opened.error());
  }
  CreatePhysics(state);
  if (!state.omni_pvd->startSampling()) {
    ReleaseProcessState(state);
    return std::unexpected(RecordingError::kRefused);
  }
  ++state.users;
  return PhysxHandles{.foundation = state.foundation, .physics = state.physics};
}

}  // namespace

std::string_view DescribeRecordingError(RecordingError error) {
  switch (error) {
    case RecordingError::kWorldExists:
      return "a World already exists, and PhysX was made without the recording";
    case RecordingError::kUnavailable:
      return kNoOmniPvd;
    case RecordingError::kRefused:
      return "OmniPVD gave no writer or file stream, or would not start sampling";
  }
  return "unknown recording error";
}

struct Recording::Impl {
  Impl(PhysxHandles counted, std::filesystem::path recorded) : lease(counted), file(std::move(recorded)) {}

  PhysxLease lease;
  std::filesystem::path file;
};

Recording::Recording(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Recording::~Recording() = default;
Recording::Recording(Recording&&) noexcept = default;
Recording& Recording::operator=(Recording&&) noexcept = default;

const std::filesystem::path& Recording::File() const { return impl_->file; }

std::expected<Recording, RecordingError> StartRecording(std::string_view executable) {
  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  std::filesystem::path file =
      std::filesystem::temp_directory_path() / std::format("{}-{:%Y%m%dT%H%M%SZ}.ovd", executable, now);
  const auto handles = AcquireRecordedPhysx(file);
  if (!handles) {
    return std::unexpected(handles.error());
  }
  LOG_INFO("subsystem=physics event=recording_started path=\"{}\"", file.string());
  return Recording(std::make_unique<Recording::Impl>(*handles, std::move(file)));
}

std::optional<Recording> StartRecordingOrWarn(std::string_view executable, std::string_view key) {
  std::expected<Recording, RecordingError> started = StartRecording(executable);
  if (!started) {
    LOG_WARN("subsystem=physics event=recording_unavailable key={} reason=\"{}\"", key,
             DescribeRecordingError(started.error()));
    return std::nullopt;
  }
  return *std::move(started);
}

namespace {

// A visualization parameter's value for on: PhysX scales what it draws by it,
// so 0 draws nothing.
float ScaleOf(bool on) { return on ? 1.0F : 0.0F; }

void CopyLines(const physx::PxRenderBuffer& buffer, std::vector<VisualizationLine>& lines) {
  const physx::PxDebugLine* const begin = buffer.getLines();
  for (PxU32 i = 0; i < buffer.getNbLines(); ++i) {
    const physx::PxDebugLine& line = begin[i];
    lines.push_back(VisualizationLine{.from = FromPx(line.pos0), .to = FromPx(line.pos1), .argb = line.color0});
  }
  // A shape PhysX draws as triangles is drawn by its edges.
  const physx::PxDebugTriangle* const triangles = buffer.getTriangles();
  for (PxU32 i = 0; i < buffer.getNbTriangles(); ++i) {
    const physx::PxDebugTriangle& triangle = triangles[i];
    const math::Vec3 a = FromPx(triangle.pos0);
    const math::Vec3 b = FromPx(triangle.pos1);
    const math::Vec3 c = FromPx(triangle.pos2);
    lines.push_back(VisualizationLine{.from = a, .to = b, .argb = triangle.color0});
    lines.push_back(VisualizationLine{.from = b, .to = c, .argb = triangle.color0});
    lines.push_back(VisualizationLine{.from = c, .to = a, .argb = triangle.color0});
  }
}

}  // namespace

void World::SetVisualization(const Visualization& visualization) {
  using physx::PxVisualizationParameter;
  // Applied only on a change, as the settings are set every tick: off is off
  // whatever its flags, so an overlay closed costs nothing.
  const Visualization& applied = impl_->visualization;
  if (visualization == applied || (!visualization.on && !applied.on)) {
    return;
  }
  impl_->visualization = visualization;
  PxScene& scene = *impl_->scene;
  // Scale 0 is PhysX's own master switch: off, the scene computes nothing to draw.
  scene.setVisualizationParameter(PxVisualizationParameter::eSCALE, ScaleOf(visualization.on));
  scene.setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, ScaleOf(visualization.collision_shapes));
  scene.setVisualizationParameter(PxVisualizationParameter::eCOLLISION_AABBS, ScaleOf(visualization.bounds));
  scene.setVisualizationParameter(PxVisualizationParameter::eCONTACT_POINT, ScaleOf(visualization.contacts));
  scene.setVisualizationParameter(PxVisualizationParameter::eCONTACT_NORMAL, ScaleOf(visualization.contacts));
  scene.setVisualizationParameter(PxVisualizationParameter::eJOINT_LOCAL_FRAMES, ScaleOf(visualization.joints));
  scene.setVisualizationParameter(PxVisualizationParameter::eJOINT_LIMITS, ScaleOf(visualization.joints));
  const PxVec3 half_extent(visualization.cull_half_extent);
  scene.setVisualizationCullingBox(
      physx::PxBounds3(ToPx(visualization.cull_center) - half_extent, ToPx(visualization.cull_center) + half_extent));
  impl_->controller_manager->setDebugRenderingFlags(
      visualization.on && visualization.controller_volumes
          ? physx::PxControllerDebugRenderFlags(physx::PxControllerDebugRenderFlag::eTEMPORAL_BV |
                                                physx::PxControllerDebugRenderFlag::eCACHED_BV)
          : physx::PxControllerDebugRenderFlags(physx::PxControllerDebugRenderFlag::eNONE));
  impl_->controller_manager->getRenderBuffer().clear();
}

std::optional<std::vector<VisualizationLine>> World::StepVisualization(float delta_time) {
  if (!impl_->visualization.on || delta_time <= 0.0F) {
    return std::nullopt;
  }
  PxScene& scene = *impl_->scene;
  // PhysX draws a scene as simulate() finds it, before it moves a kinematic
  // actor to its target, so each body's actor is put where its body last
  // moved first: its capsule is drawn where the body is on this tick, not the
  // last one.
  for (const auto& [handle, record] : impl_->bodies) {
    physx::PxRigidDynamic* const actor = record.controller->getActor();
    if (PxTransform target; actor->getKinematicTarget(target)) {
      actor->setGlobalPose(target);
    }
  }
  scene.simulate(delta_time);
  scene.fetchResults(true);
  std::vector<VisualizationLine> lines;
  CopyLines(scene.getRenderBuffer(), lines);
  physx::PxRenderBuffer& controllers = impl_->controller_manager->getRenderBuffer();
  CopyLines(controllers, lines);
  controllers.clear();
  return lines;
}
#endif

}  // namespace augusta::physics
