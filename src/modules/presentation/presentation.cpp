#include "augusta/presentation.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <flecs.h>
#include <nvtx3/nvtx3.hpp>

#include "augusta/animation.h"
#include "augusta/correction.h"
#include "augusta/interpolation.h"
#include "augusta/protocol.h"

namespace augusta::presentation {

namespace {

// How far above the local player's body position (physics::BodyState's feet)
// the camera sits - same value as scene_loader.cpp's own kEyeHeight, which
// still places the one-shot initial camera SetScene uploads before the
// first RunFrame call overwrites it via SetCamera (renderer.h).
constexpr float kEyeHeight = 1.7F;

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
  std::optional<protocol::SessionId> local_session;
  std::optional<harness::AuthoritativeState> authoritative_state;
  std::optional<harness::MatchStart> match_start;

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
  // phase actually has. The tick of the last authoritative_state recorded
  // into remote_interpolator, so a repeated Authoritative State (the network
  // thread hasn't received a new tick since the last RunFrame call) is not
  // recorded again.
  RemoteInterpolator remote_interpolator;
  float render_clock = 0.0F;
  std::optional<std::uint32_t> last_recorded_tick;
  std::vector<RemotePlayer> remote_players;

  State frame_state;

  explicit Impl(audio::Engine& engine) : audio_engine(engine) {
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
    if (!authoritative_state.has_value()) {
      remote_interpolator.Sync({});
      last_recorded_tick.reset();
    } else if (!last_recorded_tick.has_value() || *last_recorded_tick != authoritative_state->tick) {
      std::vector<protocol::SessionId> present;
      present.reserve(authoritative_state->players.size());
      for (const harness::PlayerBody& player : authoritative_state->players) {
        if (local_session.has_value() && player.session == *local_session) {
          continue;
        }
        present.push_back(player.session);
        remote_interpolator.Record(player.session, render_clock, player.body);
      }
      remote_interpolator.Sync(present);
      last_recorded_tick = authoritative_state->tick;
    }
    remote_players = remote_interpolator.Sample(render_clock - kInterpolationDelay);
    for (RemotePlayer& remote : remote_players) {
      remote.character = CharacterOf(remote.session);
    }
  }

  // The character Match start gave session, or 0 if it names no such player.
  [[nodiscard]] std::uint8_t CharacterOf(protocol::SessionId session) const {
    if (match_start.has_value()) {
      for (const harness::MatchPlayer& player : match_start->players) {
        if (player.session == session) {
          return player.character;
        }
      }
    }
    return 0;
  }

  void OnCamera() {
    const nvtx3::scoped_range range{"Camera"};
    // local_offset is already this frame's value - OnInterpolation (the
    // previous phase) just updated it. Same base position as OnCommit's
    // local_position, plus eye height, turned where the local player looks.
    camera.position = latest_state.local_body.position + local_offset + math::Vec3(0.0F, kEyeHeight, 0.0F);
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

World::World(audio::Engine& audio_engine) : impl_(std::make_unique<Impl>(audio_engine)) {}

World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

State World::RunFrame(const prediction::State& latest, const math::Quat& view_rotation,
                      std::optional<protocol::SessionId> local_session,
                      const std::optional<harness::AuthoritativeState>& authoritative,
                      const std::optional<harness::MatchStart>& match_start) {
  impl_->latest_state = latest;
  impl_->view_rotation = view_rotation;
  impl_->local_session = local_session;
  impl_->authoritative_state = authoritative;
  impl_->match_start = match_start;
  impl_->ecs.progress();
  impl_->previous_state = latest;
  impl_->has_previous_state = true;
  return impl_->frame_state;
}

}  // namespace augusta::presentation
