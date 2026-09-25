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
  // (physics::BodyState's feet): its character's eye (World's constructor).
  math::Vec3 eye;
  animation::Engine animation;
  PhaseEntities phases;
  // The previous call's latest, for kInterpolation to blend against (see
  // RunFrame's doc comment in presentation.h). Unset until the first
  // RunFrame call completes.
  prediction::State previous_state;
  bool has_previous_state = false;

  // Staged by RunFrame() immediately before ecs.progress(), read by the phase
  // systems below; not meaningful outside of a RunFrame call.
  prediction::State latest_state;
  math::Quat view_rotation{1.0F, 0.0F, 0.0F, 0.0F};
  std::optional<SessionId> local_session;
  std::optional<WorldSnapshot> snapshot;
  std::vector<PlayerCharacter> characters;

  // Hides the jumps reconciliation makes to the predicted body (ADR-0004), as
  // an offset from the predicted position that fades.
  Correction correction;
  math::Vec3 local_offset{};

  // This frame's view camera (Phase::kCamera), copied into frame_state by
  // OnCommit the same way local_offset feeds frame_state.local_position.
  Camera camera{};

  // Every other player's buffered updates (see interpolation.h), and the
  // running clock RunFrame's render frame deltas advance - independent of the
  // server's own tick clock, since a render frame's delta_time is what this
  // phase actually has. The tick of the last snapshot recorded into
  // remote_interpolator, so a repeated snapshot (the network thread hasn't
  // received a new tick since the last RunFrame call) is not recorded again.
  RemoteInterpolator remote_interpolator;
  float render_clock = 0.0F;
  std::optional<std::uint32_t> last_recorded_tick;
  std::vector<RemotePlayer> remote_players;

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
    local_offset = correction.Update(latest_state.total_correction, delta_time);
    // TODO(sergioffpc): blend the last two prediction::State values.

    render_clock += delta_time;
    // Outside a match there is no one to show (ADR-0043).
    if (!snapshot.has_value()) {
      remote_interpolator.Sync({});
      last_recorded_tick.reset();
    } else if (!last_recorded_tick.has_value() || snapshot->tick > *last_recorded_tick) {
      std::vector<SessionId> present;
      present.reserve(snapshot->bodies.size());
      for (const DynamicBody& body : snapshot->bodies) {
        if (local_session.has_value() && body.session == *local_session) {
          continue;
        }
        present.push_back(body.session);
        remote_interpolator.Record(body.session, render_clock, body.state);
      }
      remote_interpolator.Sync(present);
      last_recorded_tick = snapshot->tick;
    }
    remote_players = remote_interpolator.Sample(render_clock - kInterpolationDelay);
    for (RemotePlayer& remote : remote_players) {
      remote.character = CharacterOf(remote.session);
    }
  }

  // The character characters gives session, or 0 if it names no such player.
  [[nodiscard]] std::uint8_t CharacterOf(SessionId session) const {
    for (const PlayerCharacter& player : characters) {
      if (player.session == session) {
        return player.character;
      }
    }
    return 0;
  }

  void OnCamera() {
    const nvtx3::scoped_range range{"Camera"};
    // local_offset is already this frame's value - OnInterpolation (the
    // previous phase) just updated it. Same base position as OnCommit's
    // local_position, plus the character's eye - added as authored, since the
    // character is drawn unrotated - turned where the local player looks.
    camera.position = latest_state.local_body.position + local_offset + eye;
    camera.rotation = view_rotation;
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
    frame_state.local_position = latest_state.local_body.position + local_offset;
    frame_state.camera = camera;
    frame_state.remote_players = remote_players;
  }
};

World::World(audio::Engine& audio_engine, const math::Vec3& eye) : impl_(std::make_unique<Impl>(audio_engine, eye)) {}

World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

State World::RunFrame(const prediction::State& latest, const math::Quat& view_rotation,
                      std::optional<SessionId> local_session, const std::optional<WorldSnapshot>& snapshot,
                      std::span<const PlayerCharacter> characters) {
  impl_->latest_state = latest;
  impl_->view_rotation = view_rotation;
  impl_->local_session = local_session;
  impl_->snapshot = snapshot;
  impl_->characters.assign(characters.begin(), characters.end());
  impl_->ecs.progress();
  impl_->previous_state = latest;
  impl_->has_previous_state = true;
  return impl_->frame_state;
}

}  // namespace augusta::presentation
