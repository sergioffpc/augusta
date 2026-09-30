#include "augusta/presentation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <flecs.h>
#include <nvtx3/nvtx3.hpp>

#include "augusta/animation.h"
#include "augusta/audio.h"
#include "augusta/correction.h"
#include "augusta/interpolation.h"
#include "augusta/local_view.h"
#include "augusta/math.h"
#include "augusta/prediction.h"

namespace augusta::presentation {

namespace {

constexpr std::size_t kPhaseCount = 5;
using PhaseEntities = std::array<flecs::entity, kPhaseCount>;

enum PhaseIndex : std::size_t {
  kInterpolation = 0,
  kCamera,
  kAnimation,
  kAudioCues,
  kCommit,
};

}  // namespace

struct World::Impl {
  flecs::world ecs;
  audio::Engine& audio_engine;
  // Where the camera sits relative to the local player's body position
  // (physics::BodyState's feet) standing: its character's eye (World's
  // constructor).
  math::Vec3 eye;
  animation::Engine animation;
  PhaseEntities phases;

  // Staged by RunFrame() immediately before ecs.progress(), read by the phase
  // systems below; not meaningful outside of a RunFrame call.
  PredictedTicks ticks;
  math::Quat view_rotation{1.0F, 0.0F, 0.0F, 0.0F};
  std::optional<EntityId> local_entity;
  std::optional<WorldSnapshot> snapshot;
  std::vector<PlayerCharacter> characters;

  // The local player's Prediction State blended for this frame
  // (OnInterpolation), which the later phases read.
  prediction::State shown;

  // Hides the jumps reconciliation makes to the predicted body (ADR-0004), as
  // an offset from the predicted position that fades.
  Correction correction;
  math::Vec3 local_offset{};

  // This frame's view camera (Phase::kCamera), copied into frame_state by
  // OnCommit the same way local_offset feeds frame_state.local_position.
  Camera camera{};

  // Every other player's buffered updates, on the server's timeline, and the
  // render side's estimate of that timeline's current time, which render frame
  // deltas advance (see interpolation.h). The ticks of the first and the last
  // snapshot recorded into remote_interpolator in the match: the last, so a
  // repeated snapshot (the network thread hasn't received a new tick since the
  // last RunFrame call) is not recorded again, and both for the frame's view,
  // which is of nothing outside what there is to show.
  RemoteInterpolator remote_interpolator;
  ServerClock server_clock;
  std::optional<std::uint32_t> first_recorded_tick;
  std::optional<std::uint32_t> last_recorded_tick;
  std::vector<RemotePlayer> remote_players;
  std::optional<ShownView> view;

  State frame_state;

  Impl(audio::Engine& engine, const math::Vec3& local_eye) : audio_engine(engine), eye(local_eye) {
    // Chain the five phases in Phase's declared order (ADR-0024): each
    // depends_on the previous one, and the first depends on Flecs's
    // built-in OnUpdate phase, so a single ecs.progress() call runs them
    // in exactly this sequence.
    phases[kInterpolation] = ecs.entity("Interpolation").add(flecs::Phase).depends_on(flecs::OnUpdate);
    phases[kCamera] = ecs.entity("Camera").add(flecs::Phase).depends_on(phases[kInterpolation]);
    phases[kAnimation] = ecs.entity("Animation").add(flecs::Phase).depends_on(phases[kCamera]);
    phases[kAudioCues] = ecs.entity("AudioCues").add(flecs::Phase).depends_on(phases[kAnimation]);
    phases[kCommit] = ecs.entity("Commit").add(flecs::Phase).depends_on(phases[kAudioCues]);

    // One system per phase, matching the responsibility documented on
    // Phase's matching enumerator in presentation.h. Each uses run()
    // rather than each(): it fires exactly once per RunFrame regardless
    // of matched entities, since ECS component shapes aren't designed
    // yet (see presentation.h's header comment). Bodies are stubs until
    // those shapes exist, and until RunFrame's per-call latest argument
    // has somewhere to flow into the ECS (a singleton, presumably, once
    // one is designed).
    ecs.system("InterpolationSystem").kind(phases[kInterpolation]).run([this](flecs::iter& sys_iter) {
      OnInterpolation(sys_iter.delta_time());
    });
    ecs.system("CameraSystem").kind(phases[kCamera]).run([this](flecs::iter&) { OnCamera(); });
    ecs.system("AnimationSystem").kind(phases[kAnimation]).run([this](flecs::iter&) { OnAnimation(); });
    ecs.system("AudioCuesSystem").kind(phases[kAudioCues]).run([this](flecs::iter&) { OnAudioCues(); });
    ecs.system("CommitSystem").kind(phases[kCommit]).run([this](flecs::iter&) { OnCommit(); });
  }

  void OnInterpolation(float delta_time) {
    const nvtx3::scoped_range range{"Interpolation"};
    shown = BlendTicks(ticks.previous, ticks.latest, ticks.fraction);
    local_offset = correction.Update(shown.total_correction, delta_time);

    server_clock.Advance(delta_time);
    // Outside a match there is no one to show (ADR-0043).
    if (!snapshot.has_value()) {
      remote_interpolator.Sync({});
      server_clock.Reset();
      first_recorded_tick.reset();
      last_recorded_tick.reset();
    } else if (!last_recorded_tick.has_value() || snapshot->tick > *last_recorded_tick) {
      RecordSnapshot(*snapshot);
    }
    remote_players.clear();
    view.reset();
    if (const std::optional<double> now = server_clock.Now(); now.has_value() && snapshot.has_value()) {
      const double sample_time = *now - kInterpolationDelay;
      remote_players = remote_interpolator.Sample(sample_time);
      view = ViewAt(sample_time, snapshot->tick_duration, *first_recorded_tick, *last_recorded_tick);
    }
    for (RemotePlayer& remote : remote_players) {
      remote.character = CharacterOf(remote.entity);
    }
  }

  // Records every body in world but the local player's at world's time on the
  // server's timeline, and forgets whoever it no longer holds.
  void RecordSnapshot(const WorldSnapshot& world) {
    const double server_time = static_cast<double>(world.tick) * world.tick_duration;
    server_clock.Observe(server_time);
    std::vector<EntityId> present;
    present.reserve(world.bodies.size());
    for (const DynamicBody& body : world.bodies) {
      if (local_entity.has_value() && body.entity == *local_entity) {
        continue;
      }
      present.push_back(body.entity);
      remote_interpolator.Record(body.entity, server_time, body.state);
    }
    remote_interpolator.Sync(present);
    if (!first_recorded_tick.has_value()) {
      first_recorded_tick = world.tick;
    }
    last_recorded_tick = world.tick;
  }

  // The character of the player whose body entity is, or 0 if none is.
  [[nodiscard]] std::uint8_t CharacterOf(EntityId entity) const {
    for (const PlayerCharacter& player : characters) {
      if (player.entity == entity) {
        return player.character;
      }
    }
    return 0;
  }

  void OnCamera() {
    const nvtx3::scoped_range range{"Camera"};
    // shown and local_offset are already this frame's - OnInterpolation (the
    // previous phase) just updated them. Same base position as OnCommit's
    // local_position.
    camera = LocalCamera(shown.local_body.position + local_offset, shown.local_body.stance, eye, view_rotation);
  }

  void OnAnimation() {
    const nvtx3::scoped_range range{"Animation"};
    // TODO(sergioffpc): animation.Update per visible player character,
    // once there's a per-character handle to iterate and a
    // animation::LocomotionInput to build from interpolated movement -
    // see presentation.h's Phase::kAnimation doc comment. The capture
    // only proves animation is reachable from here; no call is made
    // yet.
    (void)animation;
  }

  void OnAudioCues() {
    const nvtx3::scoped_range range{"AudioCues"};
    // TODO(sergioffpc): audio_engine.SetListener then PlaySound per
    // this frame's cues - see presentation.h's Phase::kAudioCues doc
    // comment. The capture only proves audio_engine is reachable from
    // here; no call is made yet.
    (void)audio_engine;
  }

  void OnCommit() {
    const nvtx3::scoped_range range{"Commit"};
    frame_state.local_position = shown.local_body.position + local_offset;
    frame_state.camera = camera;
    frame_state.remote_players = remote_players;
    frame_state.view = view;
  }
};

World::World(audio::Engine& audio_engine, const math::Vec3& eye) : impl_(std::make_unique<Impl>(audio_engine, eye)) {}

World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

State World::RunFrame(const PredictedTicks& ticks, const math::Quat& view_rotation,
                      std::optional<EntityId> local_entity, const std::optional<WorldSnapshot>& snapshot,
                      std::span<const PlayerCharacter> characters) {
  impl_->ticks = ticks;
  impl_->view_rotation = view_rotation;
  impl_->local_entity = local_entity;
  impl_->snapshot = snapshot;
  impl_->characters.assign(characters.begin(), characters.end());
  impl_->ecs.progress();
  return impl_->frame_state;
}

}  // namespace augusta::presentation
