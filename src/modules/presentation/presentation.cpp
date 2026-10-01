#include "augusta/presentation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <flecs.h>
#include <nvtx3/nvtx3.hpp>

#include "augusta/animation.h"
#include "augusta/audio.h"
#include "augusta/audio_cues.h"
#include "augusta/command.h"
#include "augusta/correction.h"
#include "augusta/cues.h"
#include "augusta/effects.h"
#include "augusta/interpolation.h"
#include "augusta/local_view.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/tick.h"
#include "augusta/tracers.h"

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
  // Every cue's sound, loaded into audio_engine, index for index with
  // audio::kCues.
  std::array<audio::SoundHandle, audio::kCues.size()> cue_sounds{};
  // Where the camera sits relative to the local player's body position
  // (physics::BodyState's feet) standing: its character's eye (World's
  // constructor).
  math::Vec3 eye;
  animation::Engine animation;
  PhaseEntities phases;

  // Staged by RunFrame() immediately before ecs.progress(), read by the phase
  // systems below; not meaningful outside of a RunFrame call.
  FrameInput input;

  // What the server sent when it admitted this client (SetParameters): how a
  // Shot's tracer flies, and the ADS field of view. nullopt until then.
  std::optional<TracerRules> tracer_rules;
  float ads_field_of_view = kHipFieldOfView;

  // The Map alone, which tracers meet: a physics::World of its own, never
  // ticked, holding no body - the prediction's is the Simulation thread's.
  physics::World map{physics::StaminaConfig{}};
  Tracers tracers{map};
  std::vector<Effect> muzzle_flashes;
  FiredRounds fired_rounds;
  // How many rounds the local player's predicted fire fired this frame
  // (OnCamera), which OnAudioCues hears.
  std::uint32_t rounds_fired = 0;

  // The local player's Prediction State blended for this frame
  // (OnInterpolation), which the later phases read.
  prediction::State shown;

  // Hides the jumps reconciliation makes to the predicted body (ADR-0004), as
  // an offset from the predicted position that fades.
  Correction correction;
  math::Vec3 local_offset{};

  // This frame's view camera and what shows over it (Phase::kCamera), copied
  // into frame_state by OnCommit the same way local_offset feeds
  // frame_state.local_position.
  Camera camera{};
  AdsZoom ads_zoom;
  HitMarker hit_marker;
  bool hit_marker_shown = false;

  // Whether the local player is a spectator this frame (OnCamera), whom it
  // watches, and every other character's eye it might watch from
  // (SetCharacterEye).
  bool spectating = false;
  Spectator spectator;
  std::map<std::uint8_t, math::Vec3> character_eyes;

  // Every other player's buffered updates, on the server's timeline, and the
  // render side's estimate of that timeline's current time, which render frame
  // deltas advance (see interpolation.h). The ticks of the first and the last
  // snapshot recorded into remote_interpolator in the match: the last, so a
  // repeated snapshot (the network thread hasn't received a new tick since the
  // last RunFrame call) is not recorded again, and both for the frame's view,
  // which is of nothing outside what there is to show.
  RemoteInterpolator remote_interpolator;
  ServerClock server_clock;
  std::optional<tick::Tick> first_recorded_tick;
  std::optional<tick::Tick> last_recorded_tick;
  std::vector<RemotePlayer> remote_players;
  std::optional<ShownView> view;
  // The bodies of the match in progress whose Death has arrived: shown no
  // more, whatever update still lists them.
  std::vector<EntityId> dead;

  State frame_state;

  Impl(audio::Engine& engine, const audio::CueSounds& sounds, const math::Vec3& local_eye)
      : audio_engine(engine), eye(local_eye) {
    for (std::size_t i = 0; i < sounds.size(); ++i) {
      cue_sounds[i] = audio_engine.LoadSound(sounds[i]);
    }
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
    ecs.system("CameraSystem").kind(phases[kCamera]).run([this](flecs::iter& sys_iter) {
      OnCamera(sys_iter.delta_time());
    });
    ecs.system("AnimationSystem").kind(phases[kAnimation]).run([this](flecs::iter&) { OnAnimation(); });
    ecs.system("AudioCuesSystem").kind(phases[kAudioCues]).run([this](flecs::iter&) { OnAudioCues(); });
    ecs.system("CommitSystem").kind(phases[kCommit]).run([this](flecs::iter&) { OnCommit(); });
  }

  void OnInterpolation(float delta_time) {
    const nvtx3::scoped_range range{"Interpolation"};
    shown = BlendTicks(input.ticks.previous, input.ticks.latest, input.ticks.fraction);
    local_offset = correction.Update(shown.total_correction, delta_time);

    server_clock.Advance(delta_time);
    const std::optional<WorldSnapshot>& snapshot = input.snapshot;
    // Outside a match there is no one to show (ADR-0043).
    if (!snapshot.has_value()) {
      remote_interpolator.Sync({});
      server_clock.Reset();
      first_recorded_tick.reset();
      last_recorded_tick.reset();
      dead.clear();
      spectator = Spectator{};
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
    // A Death is reliable and can overtake the update that no longer lists its body.
    dead.insert(dead.end(), input.deaths.begin(), input.deaths.end());
    std::erase_if(remote_players,
                  [&](const RemotePlayer& remote) { return std::ranges::contains(dead, remote.entity); });
    for (RemotePlayer& remote : remote_players) {
      remote.character = CharacterOf(remote.entity);
    }
    ShowShots(delta_time);
  }

  // Moves every tracer and muzzle flash on by delta_time, then starts a tracer
  // for each of the frame's Shots and shows the muzzle flash of each fired by
  // another player: the local player's own come from its predicted fire
  // (OnCamera), which is sooner.
  void ShowShots(float delta_time) {
    tracers.Advance(delta_time);
    Age(muzzle_flashes, delta_time, kMuzzleFlashSeconds);
    for (const Shot& shot : input.shots) {
      const math::Vec3 direction = command::ViewDirection(shot.yaw, shot.pitch);
      if (tracer_rules.has_value()) {
        tracers.Fire(shot.origin, direction, *tracer_rules);
      }
      if (shot.shooter != input.local_entity) {
        muzzle_flashes.push_back(Effect{.position = MuzzleOf(shot.origin, direction)});
      }
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
      if (body.entity == input.local_entity) {
        continue;
      }
      present.push_back(body.entity);
      remote_interpolator.Record(body.entity, server_time, body.state, body.yaw);
    }
    remote_interpolator.Sync(present);
    if (!first_recorded_tick.has_value()) {
      first_recorded_tick = world.tick;
    }
    last_recorded_tick = world.tick;
  }

  // The character of the player whose body entity is, or 0 if none is.
  [[nodiscard]] std::uint8_t CharacterOf(EntityId entity) const {
    for (const PlayerCharacter& player : input.characters) {
      if (player.entity == entity) {
        return player.character;
      }
    }
    return 0;
  }

  void OnCamera(float delta_time) {
    const nvtx3::scoped_range range{"Camera"};
    // The local player's Death clears with the match (OnInterpolation).
    spectating = input.local_entity.has_value() && std::ranges::contains(dead, *input.local_entity);
    if (spectating) {
      WatchLivingPlayer();
      // Let go of ADS, so the next match starts from the hip.
      (void)ads_zoom.Update(false, ads_field_of_view, delta_time);
    } else {
      // shown and local_offset are already this frame's - OnInterpolation (the
      // previous phase) just updated them. Same base position as OnCommit's
      // local_position.
      camera = LocalCamera(shown.local_body.position + local_offset, shown.local_body.stance, eye, input.aim,
                           shown.rifle.recoil);
      camera.vertical_fov = ads_zoom.Update(input.aim.ads, ads_field_of_view, delta_time);
    }
    hit_marker_shown = hit_marker.Update(input.hit_confirmations, delta_time);
    // Flashed where the camera now is, the frame the round fires.
    rounds_fired = fired_rounds.Update(shown.total_rounds_fired);
    if (rounds_fired > 0) {
      const math::Vec3 forward = camera.rotation * math::Vec3(0.0F, 0.0F, -1.0F);
      muzzle_flashes.push_back(Effect{.position = MuzzleOf(camera.position, forward)});
    }
  }

  // A spectator's camera: at the eye of the living player it watches, as
  // remote_players shows it this frame. With no one left alive it holds where
  // it was, from the hip.
  void WatchLivingPlayer() {
    std::vector<EntityId> players;
    players.reserve(input.characters.size());
    for (const PlayerCharacter& player : input.characters) {
      players.push_back(player.entity);
    }
    // remote_players holds neither the local player nor anyone dead or gone.
    std::vector<EntityId> living;
    living.reserve(remote_players.size());
    for (const RemotePlayer& remote : remote_players) {
      living.push_back(remote.entity);
    }
    const std::optional<EntityId> watched = spectator.Update(players, living, input.fire);
    const auto shown_watched =
        std::ranges::find_if(remote_players, [&](const RemotePlayer& remote) { return remote.entity == watched; });
    if (shown_watched == remote_players.end()) {
      camera.vertical_fov = kHipFieldOfView;
      return;
    }
    camera = WatchedCamera(shown_watched->body, EyeOf(shown_watched->character));
  }

  // The eye standing of character, or the local player's character's if it was never set.
  [[nodiscard]] math::Vec3 EyeOf(std::uint8_t character) const {
    const auto found = character_eyes.find(character);
    return found != character_eyes.end() ? found->second : eye;
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
    audio_engine.SetListener(ListenerOf(camera));
    for (const CuePlay& play : SelectCues(input, rounds_fired)) {
      const auto index = static_cast<std::size_t>(std::ranges::find(audio::kCues, play.cue) - audio::kCues.begin());
      if (play.position.has_value()) {
        audio_engine.Play(cue_sounds.at(index), *play.position);
      } else {
        audio_engine.Play(cue_sounds.at(index));
      }
    }
  }

  void OnCommit() {
    const nvtx3::scoped_range range{"Commit"};
    frame_state.local_position = shown.local_body.position + local_offset;
    frame_state.camera = camera;
    frame_state.remote_players = remote_players;
    frame_state.view = view;
    frame_state.tracers = tracers.Drawn();
    frame_state.impacts.assign(tracers.Impacts().begin(), tracers.Impacts().end());
    frame_state.muzzle_flashes = muzzle_flashes;
    frame_state.crosshair = !spectating && !input.aim.ads;
    frame_state.hit_marker = !spectating && hit_marker_shown;
  }
};

World::World(audio::Engine& audio_engine, const audio::CueSounds& cue_sounds, const math::Vec3& eye)
    : impl_(std::make_unique<Impl>(audio_engine, cue_sounds, eye)) {}

World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

std::expected<void, physics::CollisionMeshError> World::AddCollisionMesh(const physics::CollisionMesh& mesh) {
  return impl_->map.AddCollisionMesh(mesh);
}

void World::SetParameters(const parameters::Parameters& parameters, float tick_duration) {
  impl_->tracer_rules = TracerRules{
      .muzzle_velocity = parameters.rifle.muzzle_velocity,
      .bullet = {.gravity = parameters.ammo.gravity, .max_range = parameters.ammo.max_range},
      .tick_duration = tick_duration,
  };
  impl_->ads_field_of_view = parameters.rifle.ads_field_of_view;
}

void World::SetCharacterEye(std::uint8_t character, const math::Vec3& eye) {
  impl_->character_eyes.insert_or_assign(character, eye);
}

State World::RunFrame(const FrameInput& input) {
  impl_->input = input;
  impl_->ecs.progress();
  return impl_->frame_state;
}

}  // namespace augusta::presentation
