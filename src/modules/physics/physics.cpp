#include "augusta/physics.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include <PxPhysicsAPI.h>

#include "augusta/logging.h"

// M1 spike (ADR-0002): the first real (non-stub) body for this module,
// backed by PhysX's character controller (CCT) rather than a raw rigid
// dynamic - a good fit for "general body movement, stance transitions"
// (physics.h's header comment), since CCT natively supports capsule
// resize (crouch/prone) and sweep-based move() without the caller having
// to hand-roll collision response.
//
// A World only ever runs PxController::move() - it never calls
// PxScene::simulate()/fetchResults(). CCT movement is sweep-based and
// self-contained; nothing here needs the rigid-body dynamics loop, since
// every body is player-controlled and there is no other dynamic geometry
// yet (see Raycast's own doc comment on the absence of static Level
// Data).
//
// Engine convention (not yet pinned down project-wide - see
// augusta::input::Command's yaw/pitch comment): Y is up, matching both
// GLM's and PhysX's own default.
namespace augusta::physics {

namespace {

// Individual using-declarations rather than `using namespace physx` -
// Google style (ADR-0012) forbids using-directives.
using physx::PxBroadPhaseType;
using physx::PxCapsuleControllerDesc;
using physx::PxController;
using physx::PxControllerCollisionFlag;
using physx::PxControllerCollisionFlags;
using physx::PxControllerFilters;
using physx::PxControllerManager;
using physx::PxCudaContextManager;
using physx::PxCudaContextManagerDesc;
using physx::PxDefaultAllocator;
using physx::PxDefaultCpuDispatcher;
using physx::PxDefaultCpuDispatcherCreate;
using physx::PxDefaultSimulationFilterShader;
using physx::PxErrorCallback;
using physx::PxErrorCode;
using physx::PxExtendedVec3;
using physx::PxFoundation;
using physx::PxMaterial;
using physx::PxPhysics;
using physx::PxRaycastBuffer;
using physx::PxScene;
using physx::PxSceneDesc;
using physx::PxSceneFlag;
using physx::PxTolerancesScale;
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
constexpr float kStepOffset = 0.3F;
constexpr float kWalkSpeed = 3.0F;  // m/s, standing baseline.
constexpr float kSprintMultiplier = 1.6F;
constexpr float kCrouchSpeedMultiplier = 0.6F;
constexpr float kProneSpeedMultiplier = 0.3F;
constexpr float kStaticFriction = 0.5F;
constexpr float kDynamicFriction = 0.5F;
constexpr float kRestitution = 0.1F;
constexpr float kMinMoveDistance = 0.001F;  // PxController::move's own minDist parameter.
constexpr int kWorkerThreadCount = 1;

// ADR-0004 snap/blend correction: an error at or beyond kSnapDistance
// teleports the predicted body directly to the authoritative state (too
// far for a blend to look acceptable - most plausibly a respawn/teleport
// the client hasn't caught up to yet); anything closer blends by
// kBlendFactor of the remaining error per Reconcile call, so repeated
// corrections converge smoothly without overshoot.
constexpr float kSnapDistance = 2.0F;
constexpr float kBlendFactor = 0.25F;

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
// request and the current stamina, resolves whether sprint is actually
// honored (forced_walk_below can override it) and the resulting stamina.
// Pure - no PxController calls - so it stays unit-testable independent of
// PhysX; Step (mechanism half) just applies the result.
struct StaminaResult {
  bool sprinting = false;
  float stamina = 1.0F;
};

StaminaResult ResolveStamina(bool sprint_requested, float current_stamina, const StaminaConfig& config,
                             float delta_time) {
  StaminaResult result;
  result.sprinting = sprint_requested && current_stamina > config.forced_walk_below;
  if (result.sprinting) {
    result.stamina = std::max(0.0F, current_stamina - (config.deplete_per_second * delta_time));
  } else {
    result.stamina = std::min(1.0F, current_stamina + (config.regen_per_second * delta_time));
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

// Decision half of Reconcile (ADR-0004 snap/blend correction): computes the
// corrected BodyState from the predicted and authoritative states, and
// whether this correction snapped rather than blended. Pure - no
// PxController calls - so it stays unit-testable independent of PhysX;
// Reconcile (mechanism half) just applies the result to the controller.
struct ReconciliationResult {
  BodyState state;
  bool snapped = false;
  float error = 0.0F;
};

ReconciliationResult ResolveReconciliation(const BodyState& predicted, const BodyState& authoritative) {
  ReconciliationResult result;
  result.error = math::Length(authoritative.position - predicted.position);
  if (result.error >= kSnapDistance) {
    result.state = authoritative;
    result.snapped = true;
    return result;
  }
  result.state = predicted;
  result.state.position = predicted.position + ((authoritative.position - predicted.position) * kBlendFactor);
  result.state.velocity = predicted.velocity + ((authoritative.velocity - predicted.velocity) * kBlendFactor);
  result.state.stance = authoritative.stance;
  result.state.stamina = authoritative.stamina;
  return result;
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
    LE("subsystem=physics event=physx_error code={} message=\"{}\" file={} line={}", static_cast<int>(code), message,
       file, line);
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
  PxPhysics* physics = nullptr;
};

PhysxProcessState& ProcessState() {
  static PhysxProcessState state;
  return state;
}

struct PhysxHandles {
  PxFoundation* foundation;
  PxPhysics* physics;
};

PhysxHandles AcquirePhysx() {
  PhysxProcessState& state = ProcessState();
  const std::lock_guard<std::mutex> lock(state.mutex);
  if (state.users == 0) {
    state.foundation = PxCreateFoundation(PX_PHYSICS_VERSION, state.allocator, state.error_callback);
    if (state.foundation == nullptr) {
      throw std::runtime_error("physics::World: PxCreateFoundation failed");
    }
    const PxTolerancesScale scale;
    state.physics = PxCreatePhysics(PX_PHYSICS_VERSION, *state.foundation, scale, true);
    if (state.physics == nullptr) {
      state.foundation->release();
      state.foundation = nullptr;
      throw std::runtime_error("physics::World: PxCreatePhysics failed");
    }
  }
  ++state.users;
  return {.foundation = state.foundation, .physics = state.physics};
}

void ReleasePhysx() {
  PhysxProcessState& state = ProcessState();
  const std::lock_guard<std::mutex> lock(state.mutex);
  if (--state.users == 0) {
    state.physics->release();
    state.foundation->release();
    state.physics = nullptr;
    state.foundation = nullptr;
  }
}

// Holds one use of the shared PhysX state for as long as it lives. Being a
// member, it is released even if the owning World's constructor throws after
// it was acquired, when the World's destructor would not run.
class PhysxLease {
 public:
  PhysxLease() : handles_(AcquirePhysx()) {}
  ~PhysxLease() { ReleasePhysx(); }
  PhysxLease(const PhysxLease&) = delete;
  PhysxLease& operator=(const PhysxLease&) = delete;

  [[nodiscard]] PxFoundation* Foundation() const { return handles_.foundation; }
  [[nodiscard]] PxPhysics* Physics() const { return handles_.physics; }

 private:
  PhysxHandles handles_;
};

}  // namespace

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
  PxFoundation* foundation = lease.Foundation();
  PxPhysics* physics = lease.Physics();
  PxDefaultCpuDispatcher* dispatcher = nullptr;
  // Non-null only when enable_gpu was requested AND a CUDA-capable
  // GPU/driver was actually found - see the constructor. Currently has
  // no observable effect on Step's own output; see World's own header
  // comment for why it's wired in ahead of need.
  PxCudaContextManager* cuda_context_manager = nullptr;
  PxScene* scene = nullptr;
  PxControllerManager* controller_manager = nullptr;
  PxMaterial* material = nullptr;
  StaminaConfig stamina_config;
  std::unordered_map<BodyHandle, BodyRecord> bodies;
  std::uint32_t next_handle = 1;

  Impl(const StaminaConfig& config, bool enable_gpu) : stamina_config(config) {
    PxSceneDesc scene_desc(physics->getTolerancesScale());
    scene_desc.gravity = PxVec3(0.0F, kGravity, 0.0F);
    dispatcher = PxDefaultCpuDispatcherCreate(kWorkerThreadCount);
    scene_desc.cpuDispatcher = dispatcher;
    scene_desc.filterShader = PxDefaultSimulationFilterShader;

    if (enable_gpu) {
      const PxCudaContextManagerDesc cuda_desc;
      // Unqualified, not physx::PxCreateCudaContextManager: gpu/PxGpu.h
      // declares it PX_C_EXPORT (extern "C") at global scope, only its
      // parameter/return types live in namespace physx.
      cuda_context_manager = ::PxCreateCudaContextManager(*foundation, cuda_desc);
      if (cuda_context_manager != nullptr && cuda_context_manager->contextIsValid()) {
        scene_desc.cudaContextManager = cuda_context_manager;
        scene_desc.flags |= PxSceneFlag::eENABLE_GPU_DYNAMICS;
        scene_desc.broadPhaseType = PxBroadPhaseType::eGPU;
        LI("subsystem=physics event=gpu_context_created device=\"{}\"", cuda_context_manager->getDeviceName());
      } else {
        // No CUDA-capable GPU/driver on this machine - fall back to CPU
        // rather than fail World construction over it.
        LW("subsystem=physics event=gpu_unavailable fallback=cpu");
        if (cuda_context_manager != nullptr) {
          cuda_context_manager->release();
          cuda_context_manager = nullptr;
        }
      }
    }

    scene = physics->createScene(scene_desc);
    controller_manager = PxCreateControllerManager(*scene);
    material = physics->createMaterial(kStaticFriction, kDynamicFriction, kRestitution);
    LD("subsystem=physics event=world_created gpu={}", cuda_context_manager != nullptr);
  }

  ~Impl() {
    for (auto& [handle, record] : bodies) {
      if (record.controller != nullptr) {
        record.controller->release();
      }
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
    // Released after the scene (which references it), matching
    // PxCudaContextManager::release()'s own documented ordering
    // requirement - never released while a scene is still using it.
    if (cuda_context_manager != nullptr) {
      cuda_context_manager->release();
    }
    if (dispatcher != nullptr) {
      dispatcher->release();
    }
  }
};

World::World(const StaminaConfig& config, bool enable_gpu) : impl_(std::make_unique<Impl>(config, enable_gpu)) {}
World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

BodyHandle World::CreateBody(const math::Vec3& initial_position) {
  PxCapsuleControllerDesc desc;
  desc.radius = kCapsuleRadius;
  desc.height = kStandingHeight;
  desc.position = PxExtendedVec3(initial_position.x, initial_position.y, initial_position.z);
  desc.material = impl_->material;
  desc.stepOffset = kStepOffset;
  desc.upDirection = PxVec3(0.0F, 1.0F, 0.0F);
  PxController* controller = impl_->controller_manager->createController(desc);
  if (controller == nullptr) {
    throw std::runtime_error("physics::World::CreateBody: createController failed");
  }

  const auto handle = static_cast<BodyHandle>(impl_->next_handle++);
  BodyRecord record;
  record.controller = controller;
  record.state.position = initial_position;
  impl_->bodies.emplace(handle, std::move(record));
  LD("subsystem=physics event=body_created handle={}", static_cast<std::uint32_t>(handle));
  return handle;
}

void World::DestroyBody(BodyHandle handle) {
  const auto body_it = impl_->bodies.find(handle);
  if (body_it == impl_->bodies.end()) {
    return;
  }
  body_it->second.controller->release();
  impl_->bodies.erase(body_it);
  LD("subsystem=physics event=body_destroyed handle={}", static_cast<std::uint32_t>(handle));
}

BodyState World::Step(BodyHandle handle, const MovementInput& input, float delta_time) {
  const auto body_it = impl_->bodies.find(handle);
  if (body_it == impl_->bodies.end()) {
    return {};
  }
  BodyRecord& record = body_it->second;
  BodyState& state = record.state;

  if (input.desired_stance != state.stance) {
    // TODO(sergioffpc): reject the transition when the target capsule
    // would overlap static geometry (e.g. standing up under a low
    // ceiling) - there is no static Level Data to collide against yet
    // (see Raycast's own doc comment above), so every transition
    // currently succeeds.
    record.controller->resize(HeightForStance(input.desired_stance));
    state.stance = input.desired_stance;
  }

  const StaminaResult stamina = ResolveStamina(input.sprint, state.stamina, impl_->stamina_config, delta_time);
  state.stamina = stamina.stamina;

  const math::Vec3 direction = math::Normalize(input.direction);
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

  const math::Vec3 new_position = FromPx(record.controller->getFootPosition());
  state.velocity = delta_time > 0.0F ? (new_position - state.position) / delta_time : math::Vec3{};
  state.position = new_position;

  return state;
}

void World::SetState(BodyHandle handle, const BodyState& state) {
  const auto body_it = impl_->bodies.find(handle);
  if (body_it == impl_->bodies.end()) {
    return;
  }
  BodyRecord& record = body_it->second;
  if (state.stance != record.state.stance) {
    record.controller->resize(HeightForStance(state.stance));
  }
  record.controller->setFootPosition(PxExtendedVec3(state.position.x, state.position.y, state.position.z));
  record.state = state;
  record.vertical_speed = 0.0F;
  record.grounded = false;
}

BodyState World::Reconcile(BodyHandle handle, const BodyState& authoritative) {
  const auto body_it = impl_->bodies.find(handle);
  if (body_it == impl_->bodies.end()) {
    return authoritative;
  }
  BodyRecord& record = body_it->second;
  const ReconciliationResult result = ResolveReconciliation(record.state, authoritative);
  LD("subsystem=physics event={} handle={} error={:.3f}", result.snapped ? "reconcile_snap" : "reconcile_blend",
     static_cast<std::uint32_t>(handle), result.error);

  // Applied directly against the controller (not via SetState): SetState
  // also resets fall/ground tracking, which is correct for an intentional
  // teleport (spawn/respawn) but would spuriously interrupt gravity
  // continuity for what is, outside of the kSnapDistance case above, a
  // small in-place correction.
  if (result.state.stance != record.state.stance) {
    record.controller->resize(HeightForStance(result.state.stance));
  }
  record.controller->setFootPosition(
      PxExtendedVec3(result.state.position.x, result.state.position.y, result.state.position.z));
  record.state = result.state;
  return result.state;
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

}  // namespace augusta::physics
