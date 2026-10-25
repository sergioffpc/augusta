#include "augusta/prediction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <utility>
#if AUGUSTA_DEBUG_TOOLS
#include <vector>
#endif

#include <flecs.h>
#include <nvtx3/nvtx3.hpp>

#include "augusta/clips.h"
#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/locomotion.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/reconciliation.h"
#include "augusta/tick.h"
#include "augusta/weapon.h"

namespace augusta::prediction {

namespace {

// The Surface id every mesh of the Map is added with: nothing else here is one.
constexpr physics::SurfaceId kMapSurface{0};

constexpr std::size_t kPhaseCount = 5;
using PhaseEntities = std::array<flecs::entity, kPhaseCount>;

// How far the server's state may be from the client's own prediction of the
// same command and still count as agreeing with it. Below this nothing is
// restored or replayed: the error is not worth a jump, and it cannot pile up,
// since the next acknowledgement is compared with the server's state again,
// not with this one.
constexpr float kPositionTolerance = 0.001F;  // 1 mm.
constexpr float kStaminaTolerance = 0.001F;

// A Stride a step apart is the rounding of a distance within kPositionTolerance.
constexpr int kStrideTolerance = 1;

// How far apart two Strides are, the short way round the cycle.
int StrideDistance(std::uint8_t a, std::uint8_t b) {
  const int ahead = (a - b + physics::kStrideSteps) % physics::kStrideSteps;
  return std::min(ahead, physics::kStrideSteps - ahead);
}

// Position is what the player sees, but a stance, an exhaustion or a stamina
// that differs changes what the next commands do, so those count as well; and a
// Stride, a grounded flag or a change of stance's progress, which pose the body
// every side sees. The progress advances by whole steps of time alone, so any
// difference is a misprediction, never rounding.
bool BodyNeedsCorrection(const physics::BodyState& authoritative, const physics::BodyState& predicted) {
  return math::Length(authoritative.position - predicted.position) >= kPositionTolerance ||
         authoritative.stance != predicted.stance || authoritative.exhausted != predicted.exhausted ||
         authoritative.grounded != predicted.grounded ||
         std::abs(authoritative.stamina - predicted.stamina) >= kStaminaTolerance ||
         StrideDistance(authoritative.stride, predicted.stride) > kStrideTolerance ||
         authoritative.previous_stance != predicted.previous_stance ||
         authoritative.stance_progress != predicted.stance_progress;
}

enum PhaseIndex : std::size_t {
  kCommandIngestion = 0,
  kReconciliation,
  kMovement,
  kWeaponHandling,
  kCommit,
};

}  // namespace

struct World::Impl {
  flecs::world ecs;
  physics::World physics;
  // The local player's Character's Clips (World's constructor).
  std::shared_ptr<const animation::CharacterClips> clips;
  // M1 spike (issue #32): the one entity this pipeline predicts, ahead of
  // real ECS component shapes (see prediction.h's header comment) -
  // spawned once here rather than discovered via a component query.
  physics::BodyHandle local_body;
  // The local player's rifle and the server's values for it. Until Start gives
  // them, a rifle with no round: it fires nothing.
  weapon::Rifle rifle_rules;
  weapon::State rifle;
  PhaseEntities phases;

  // Staged by Tick() immediately before each ecs.progress() call, read by
  // the phase systems below; not meaningful outside of a Tick call. Once
  // real ECS component shapes exist, CommandIngestion applying this to an
  // entity (rather than the systems closing over it directly) is what
  // replaces this.
  command::Command tick_command;
  tick::Sequence tick_sequence = 0;
  std::optional<Acknowledgement> tick_acknowledgement;
  State tick_state;

  // The commands sent and what was predicted after each, for Reconciliation to
  // compare the server's answer with and to replay from it.
  History history;

  explicit Impl(std::shared_ptr<const animation::CharacterClips> character_clips)
      // No stamina rules of its own: Start gives the world the server's.
      : physics(physics::StaminaConfig{}),
        clips(std::move(character_clips)),
        local_body(physics.CreateBody(math::Vec3(0.0F, 0.0F, 0.0F))) {
    // Chain the five phases in Phase's declared order (ADR-0024): each
    // depends_on the previous one, and the first depends on Flecs's
    // built-in OnUpdate phase, so a single ecs.progress() call runs them
    // in exactly this sequence.
    phases[kCommandIngestion] = ecs.entity("CommandIngestion").add(flecs::Phase).depends_on(flecs::OnUpdate);
    phases[kReconciliation] = ecs.entity("Reconciliation").add(flecs::Phase).depends_on(phases[kCommandIngestion]);
    phases[kMovement] = ecs.entity("Movement").add(flecs::Phase).depends_on(phases[kReconciliation]);
    phases[kWeaponHandling] = ecs.entity("WeaponHandling").add(flecs::Phase).depends_on(phases[kMovement]);
    phases[kCommit] = ecs.entity("Commit").add(flecs::Phase).depends_on(phases[kWeaponHandling]);

    // One system per phase, matching the responsibility documented on
    // Phase's matching enumerator in prediction.h. Each uses run()
    // rather than each(): it fires exactly once per Tick regardless of
    // matched entities, since ECS component shapes aren't designed yet
    // (see prediction.h's header comment) - local_body is a single
    // hardcoded handle rather than something discovered by a query.
    ecs.system("CommandIngestionSystem").kind(phases[kCommandIngestion]).run([this](flecs::iter&) {
      OnCommandIngestion();
    });
    ecs.system("ReconciliationSystem").kind(phases[kReconciliation]).run([this](flecs::iter& sys_iter) {
      OnReconciliation(sys_iter.delta_time());
    });
    ecs.system("MovementSystem").kind(phases[kMovement]).run([this](flecs::iter& sys_iter) {
      OnMovement(sys_iter.delta_time());
    });
    ecs.system("WeaponHandlingSystem").kind(phases[kWeaponHandling]).run([this](flecs::iter& sys_iter) {
      OnWeaponHandling(sys_iter.delta_time());
    });
    ecs.system("CommitSystem").kind(phases[kCommit]).run([this](flecs::iter& sys_iter) {
      OnCommit(sys_iter.delta_time());
    });
  }

  void OnCommandIngestion() {
    const nvtx3::scoped_range range{"CommandIngestion"};
    // tick_command is already staged by Tick() - nothing else to
    // ingest yet without an entity/component to apply it to.
  }

  // Puts the body and the rifle at the server's state and steps them through
  // the commands sent since, so the present is the server's past with the
  // client's own commands carried forward. Every step, here and in Movement
  // and WeaponHandling, is a fixed tick long. The rifle travels exactly (its
  // times as their bits, its Recoil offset on the grid weapon::Step keeps it
  // on) and the same function steps it on both sides, so one that differs at
  // all was predicted wrong.
  void OnReconciliation(float delta_time) {
    const nvtx3::scoped_range range{"Reconciliation"};
    if (!tick_acknowledgement.has_value()) {
      return;
    }
    const std::optional<Predicted> predicted = history.Acknowledge(tick_acknowledgement->sequence);
    if (!predicted.has_value()) {
      return;
    }
    const physics::BodyState& authoritative = tick_acknowledgement->body;
    const bool rifle_differs = tick_acknowledgement->rifle != predicted->rifle;
    if (!BodyNeedsCorrection(authoritative, predicted->body) && !rifle_differs) {
      return;
    }
    physics::BodyState replayed = physics.Restore(local_body, authoritative, predicted->fall);
    rifle = tick_acknowledgement->rifle;
    history.Replay([&](const command::Command& command) {
      replayed = Move(replayed, command, delta_time);
      rifle = weapon::Step(rifle_rules, rifle, command, delta_time).state;
      return Predicted{.body = replayed, .fall = physics.Fall(local_body), .rifle = rifle};
    });

    const math::Vec3 jump = replayed.position - tick_state.local_body.position;
    LOG_DEBUG("subsystem=predictionworld event=reconcile sequence={} error={:.3f} jump={:.3f} rifle={}",
              tick_acknowledgement->sequence, math::Length(authoritative.position - predicted->body.position),
              math::Length(jump), rifle_differs);
    tick_state.total_correction += jump;
    tick_state.local_body = replayed;
    tick_state.rifle_corrections += rifle_differs ? 1U : 0U;
  }

  void OnMovement(float delta_time) {
    const nvtx3::scoped_range range{"Movement"};
    tick_state.local_body = Move(tick_state.local_body, tick_command, delta_time);
  }

  // Steps the local body, which body is the state of, by command, paced by its
  // Clips (its Stride by the stride length at body's stance and velocity in the
  // space of command's yaw on its grid, a change of stance by its transition
  // duration): what SimulationWorld's Movement steps it by.
  physics::BodyState Move(const physics::BodyState& body, const command::Command& command, float delta_time) {
    return physics.Step(local_body, command.movement, delta_time,
                        animation::PacingOf(*clips, body, math::SnapAngle(command.yaw)));
  }

  void OnWeaponHandling(float delta_time) {
    const nvtx3::scoped_range range{"WeaponHandling"};
    const weapon::Result result = weapon::Step(rifle_rules, rifle, tick_command, delta_time);
    rifle = result.state;
    tick_state.rifle = rifle;
    tick_state.rounds_fired = result.fired ? 1 : 0;
    tick_state.total_rounds_fired += tick_state.rounds_fired;
  }

  void OnCommit([[maybe_unused]] float delta_time) {
    const nvtx3::scoped_range range{"Commit"};
#if AUGUSTA_DEBUG_TOOLS
    // Once per tick, here and never in a replay, so the lines show the body
    // where the tick left it. Null while Collision is off.
    std::optional<std::vector<physics::VisualizationLine>> lines = physics.StepVisualization(delta_time);
    tick_state.collision_lines =
        lines.has_value() ? std::make_shared<const std::vector<physics::VisualizationLine>>(std::move(*lines))
                          : nullptr;
#endif
    // tick_state is already set by OnMovement and OnWeaponHandling; what is
    // left is remembering it for the server's answer to this command.
    if (tick_sequence != 0) {
      history.Record(tick_sequence, tick_command,
                     Predicted{.body = tick_state.local_body, .fall = physics.Fall(local_body), .rifle = rifle});
    }
  }
};

World::World(std::shared_ptr<const animation::CharacterClips> clips)
    : impl_(std::make_unique<Impl>(std::move(clips))) {}

World::~World() = default;

std::expected<void, physics::CollisionMeshError> World::AddCollisionMesh(const physics::CollisionMesh& mesh) {
  return impl_->physics.AddCollisionMesh(mesh, kMapSurface);
}

void World::Start(const math::Vec3& spawn, const parameters::Parameters& parameters) {
  Impl& impl = *impl_;
  impl.physics.SetStaminaConfig(parameters.stamina);
  physics::BodyState start{};
  start.position = spawn;
  impl.physics.SetState(impl.local_body, start);
  impl.rifle_rules = parameters.rifle;
  impl.rifle = weapon::Loaded(parameters.rifle);
  impl.history = History{};
  // Starting over is not a correction: the running totals are kept, so a
  // reader sees no jump in them (see State::total_correction).
  State& state = impl.tick_state;
  state.local_body = start;
  state.rifle = impl.rifle;
  state.rounds_fired = 0;
#if AUGUSTA_DEBUG_TOOLS
  state.collision_lines = nullptr;
#endif
}

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

State World::Tick(const command::Command& command, tick::Sequence sequence,
                  const std::optional<Acknowledgement>& acknowledgement, float delta_time) {
  impl_->tick_command = command;
  impl_->tick_sequence = sequence;
  impl_->tick_acknowledgement = acknowledgement;
  impl_->ecs.progress(delta_time);
  return impl_->tick_state;
}

#if AUGUSTA_DEBUG_TOOLS
void World::SetVisualization(const physics::Visualization& visualization) {
  impl_->physics.SetVisualization(visualization);
}
#endif

}  // namespace augusta::prediction
