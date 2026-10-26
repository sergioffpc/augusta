#include "augusta/presentation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <flecs.h>
#include <nvtx3/nvtx3.hpp>

#include "audio_cues.h"
#include "augusta/animation.h"
#include "augusta/assets.h"
#include "augusta/audio.h"
#include "augusta/command.h"
#include "augusta/cues.h"
#include "augusta/effects.h"
#include "augusta/interpolation.h"
#include "augusta/local_view.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/pose.h"
#include "augusta/prediction.h"
#include "augusta/tick.h"
#include "augusta/tracers.h"
#include "augusta/weapon.h"
#include "correction.h"

namespace augusta::presentation {

namespace {

// The Surface id every mesh of the Map is added with: tracers meet nothing else.
constexpr physics::SurfaceId kMapSurface{0};

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
  // A cue's sound as loaded into audio_engine: its variants, its gain, and the
  // variant it played last.
  struct LoadedCue {
    std::vector<audio::SoundHandle> variants;
    float gain_db = 0.0F;
    std::optional<std::size_t> last;
  };
  // Every cue's sound, index for index with audio::kCues.
  std::array<LoadedCue, audio::kCues.size()> cue_sounds{};
  // What chooses each cue's variant as it plays (audio::ChooseVariant).
  std::mt19937 variant_random{std::random_device{}()};
  // The local player's character (World's constructor): its eye is where the
  // camera sits relative to its body position (physics::BodyState's feet)
  // standing, and its Skeleton, Clips and rifle are how its body holds the
  // rifle its muzzle flash shows at the Muzzle of.
  LocalCharacter local;
  // Where the local player's body is posed to place its Muzzle (OnAnimation):
  // the Poser keeps its working buffers, the pose its storage, frame to frame.
  animation::Poser poser;
  animation::SkeletonPose posed;
  PhaseEntities phases;

  // The frame RunFrame() is running, borrowed for its ecs.progress() and read
  // by the phase systems below; null outside of a RunFrame call, so nothing
  // here can hold on to what it lends (FrameInput).
  const FrameInput* input = nullptr;

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
  // (OnCamera), which OnAnimation flashes and OnAudioCues hears.
  std::uint32_t rounds_fired = 0;
  CueSelector cue_selector;

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
  std::map<std::string, math::Vec3, std::less<>> character_eyes;

  // Every other player's buffered updates, on the server's timeline, and the
  // render side's estimate of that timeline's current time, which render frame
  // deltas advance (see interpolation.h). The ticks of the first and the last
  // update recorded into remote_interpolator in the match: the last, so an
  // update a previous frame recorded (the frame's updates reach back past the
  // previous frame) is not recorded again, and both for the frame's Seen time,
  // which is of nothing outside what there is to show.
  RemoteInterpolator remote_interpolator;
  ServerClock server_clock;
  std::optional<tick::Tick> first_recorded_tick;
  std::optional<tick::Tick> last_recorded_tick;
  std::vector<RemotePlayer> remote_players;
  std::optional<SeenTime> seen_time;
  std::optional<double> interpolation_slack;
  // The bodies of the match in progress whose Death has arrived: shown no
  // more, whatever update still lists them.
  std::vector<EntityId> dead;

  State frame_state;

  Impl(audio::Engine& engine, const audio::CueSounds& sounds, LocalCharacter local_character)
      : audio_engine(engine), local(std::move(local_character)) {
    for (std::size_t i = 0; i < sounds.size(); ++i) {
      cue_sounds[i].gain_db = sounds[i].gain_db;
      for (const assets::AudioData& variant : sounds[i].variants) {
        cue_sounds[i].variants.push_back(audio_engine.LoadSound(variant));
      }
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
    shown = BlendTicks(input->ticks.previous, input->ticks.latest, input->ticks.fraction);
    local_offset = correction.Update(shown.total_correction, delta_time);

    server_clock.Advance(delta_time);
    const std::span<const WorldSnapshot> updates = input->updates;
    // Outside a match there is no one to show (ADR-0043).
    if (updates.empty()) {
      remote_interpolator.Sync({});
      server_clock.Reset();
      first_recorded_tick.reset();
      last_recorded_tick.reset();
      dead.clear();
      spectator = Spectator{};
    }
    for (const WorldSnapshot& update : updates) {
      if (!last_recorded_tick.has_value() || update.tick > *last_recorded_tick) {
        RecordUpdate(update);
      }
    }
    remote_players.clear();
    seen_time.reset();
    if (const std::optional<double> now = server_clock.Now(); now.has_value() && !updates.empty()) {
      const double sample_time = *now - kInterpolationDelay;
      remote_players = remote_interpolator.Sample(sample_time);
      seen_time = SeenTimeAt(sample_time, updates.back().tick_duration, *first_recorded_tick, *last_recorded_tick);
    }
    // A Death is reliable and can overtake the update that no longer lists its body.
    dead.insert(dead.end(), input->deaths.begin(), input->deaths.end());
    std::erase_if(remote_players,
                  [&](const RemotePlayer& remote) { return std::ranges::contains(dead, remote.entity); });
    for (RemotePlayer& remote : remote_players) {
      remote.character = CharacterOf(remote.entity);
    }
    interpolation_slack = remote_players.empty() ? std::nullopt : server_clock.Slack();
    ShowShots(delta_time);
  }

  // Moves every tracer and muzzle flash on by delta_time, then starts a tracer
  // for each of the frame's Shots and shows the muzzle flash of each fired by
  // another player, both at the shooter's Muzzle the Shot left from: the local
  // player's own flashes come from its predicted fire (OnCamera), which is
  // sooner.
  void ShowShots(float delta_time) {
    tracers.Advance(delta_time);
    Age(muzzle_flashes, delta_time, kMuzzleFlashSeconds);
    for (const Shot& shot : input->shots) {
      if (tracer_rules.has_value()) {
        tracers.Fire(shot.origin, command::ViewDirection(shot.yaw, shot.pitch), *tracer_rules);
      }
      if (shot.shooter != input->local_entity) {
        muzzle_flashes.push_back(Effect{.position = shot.origin});
      }
    }
  }

  // Records every body in world but the local player's at world's time on the
  // server's timeline, and forgets whoever it no longer holds.
  void RecordUpdate(const WorldSnapshot& world) {
    const double server_time = static_cast<double>(world.tick) * world.tick_duration;
    server_clock.Observe(server_time);
    std::vector<EntityId> present;
    present.reserve(world.bodies.size());
    for (const DynamicBody& body : world.bodies) {
      if (body.entity == input->local_entity) {
        continue;
      }
      present.push_back(body.entity);
      remote_interpolator.Record(body.entity, server_time, body.state,
                                 Aim{.yaw = body.yaw, .pitch = body.pitch, .ads = body.ads});
    }
    remote_interpolator.Sync(present);
    if (!first_recorded_tick.has_value()) {
      first_recorded_tick = world.tick;
    }
    last_recorded_tick = world.tick;
  }

  // The character of the player whose body entity is, or empty if none is.
  [[nodiscard]] std::string CharacterOf(EntityId entity) const {
    for (const PlayerCharacter& player : input->characters) {
      if (player.entity == entity) {
        return player.character;
      }
    }
    return {};
  }

  void OnCamera(float delta_time) {
    const nvtx3::scoped_range range{"Camera"};
    // The local player's Death clears with the match (OnInterpolation).
    spectating =
        input->replay_viewer || (input->local_entity.has_value() && std::ranges::contains(dead, *input->local_entity));
    if (spectating) {
      WatchLivingPlayer(delta_time);
    } else {
      // shown and local_offset are already this frame's - OnInterpolation (the
      // previous phase) just updated them. Same base position as OnCommit's
      // local_position.
      camera = LocalCamera(shown.local_body.position + local_offset, shown.local_body.stance, local.eye, input->aim,
                           shown.rifle.recoil);
      camera.vertical_fov = ads_zoom.Update(input->aim.ads, ads_field_of_view, delta_time);
    }
    hit_marker_shown = hit_marker.Update(input->hit_confirmations, delta_time);
    rounds_fired = fired_rounds.Update(shown.total_rounds_fired);
  }

  // A spectator's camera: at the eye of the living player it watches, as
  // remote_players shows it this frame, at its pitch and zoomed by its ADS.
  // With no one left alive it holds where it was, from the hip.
  void WatchLivingPlayer(float delta_time) {
    std::vector<EntityId> players;
    players.reserve(input->characters.size());
    for (const PlayerCharacter& player : input->characters) {
      players.push_back(player.entity);
    }
    // remote_players holds neither the local player nor anyone dead or gone.
    std::vector<EntityId> living;
    living.reserve(remote_players.size());
    for (const RemotePlayer& remote : remote_players) {
      living.push_back(remote.entity);
    }
    const std::optional<EntityId> watched = spectator.Update(players, living, input->fire);
    const auto shown_watched =
        std::ranges::find_if(remote_players, [&](const RemotePlayer& remote) { return remote.entity == watched; });
    const bool shown = shown_watched != remote_players.end();
    // Let go of ADS with no one watched to hold it, so the next match starts from the hip.
    const float zoomed = ads_zoom.Update(shown && shown_watched->body.ads, ads_field_of_view, delta_time);
    if (!shown) {
      camera.vertical_fov = kHipFieldOfView;
      return;
    }
    camera = WatchedCamera(shown_watched->body, EyeOf(shown_watched->character));
    camera.vertical_fov = zoomed;
  }

  // The eye standing of character, or the local player's character's if it was never set.
  [[nodiscard]] math::Vec3 EyeOf(const std::string& character) const {
    const auto found = character_eyes.find(character);
    return found != character_eyes.end() ? found->second : local.eye;
  }

  void OnAnimation() {
    const nvtx3::scoped_range range{"Animation"};
    // Flashed the frame the round fires, ahead of the Shot that announces it.
    if (rounds_fired > 0) {
      muzzle_flashes.push_back(Effect{.position = LocalMuzzle()});
    }
  }

  // Where the local player's Muzzle is this frame, placed as the server places
  // the Muzzle a round leaves from (ADR-0064): its predicted body, where it is
  // shown, posed where it aims and holding the rifle, the rifle turned about its
  // Grip by the Recoil offset the round left with. That is the one from before
  // the round's own kick: the previous tick's, when the newest tick fired it,
  // the predicted rifle's own being the kicked one. A frame spanning several
  // ticks, an earlier one of them firing, sees only its two newest, which hold
  // no offset from before that round's kick: the flash then takes the previous
  // tick's, its kick already in it, an approximation of one kick for one frame,
  // left as it is since the flash is drawn, not where any round leaves from.
  [[nodiscard]] math::Vec3 LocalMuzzle() {
    physics::BodyState body = shown.local_body;
    body.position += local_offset;
    const Aim& aim = input->aim;
    poser.Pose(*local.clips, local.skeleton,
               animation::PoseInput{.body = body, .yaw = aim.yaw, .pitch = aim.pitch, .ads = aim.ads}, posed);
    const math::Mat4 hand = animation::HoldRifle(local.skeleton, local.clips->weapon_socket, local.rifle, posed);
    const weapon::RecoilOffset& before_kick = input->ticks.previous.rifle.recoil;
    return math::TranslationOf(
        weapon::PlaceMuzzle(weapon::Recoiled(hand, aim.yaw, aim.pitch, before_kick), local.rifle));
  }

  void OnAudioCues() {
    const nvtx3::scoped_range range{"AudioCues"};
    audio_engine.SetListener(ListenerOf(camera));
    for (const CuePlay& play : cue_selector.Select(*input, rounds_fired)) {
      const auto index = static_cast<std::size_t>(std::ranges::find(audio::kCues, play.cue) - audio::kCues.begin());
      LoadedCue& cue = cue_sounds.at(index);
      // A World given no sounds (a test's) plays none.
      if (cue.variants.empty()) {
        continue;
      }
      const std::size_t variant = audio::ChooseVariant(cue.variants.size(), cue.last, variant_random());
      cue.last = variant;
      if (play.position.has_value()) {
        audio_engine.Play(cue.variants.at(variant), *play.position, cue.gain_db);
      } else {
        audio_engine.Play(cue.variants.at(variant), cue.gain_db);
      }
    }
  }

  void OnCommit() {
    const nvtx3::scoped_range range{"Commit"};
    frame_state.local_position = shown.local_body.position + local_offset;
    frame_state.camera = camera;
    // Sampled anew each frame (OnInterpolation), so handed over, not copied.
    frame_state.remote_players = std::move(remote_players);
    frame_state.seen_time = seen_time;
    frame_state.interpolation_slack = interpolation_slack;
    frame_state.tracers = tracers.Drawn();
    frame_state.impacts.assign(tracers.Impacts().begin(), tracers.Impacts().end());
    frame_state.muzzle_flashes = muzzle_flashes;
    frame_state.crosshair = !spectating && !input->aim.ads;
    frame_state.hit_marker = !spectating && hit_marker_shown;
  }
};

World::World(audio::Engine& audio_engine, const audio::CueSounds& cue_sounds, LocalCharacter local)
    : impl_(std::make_unique<Impl>(audio_engine, cue_sounds, std::move(local))) {}

World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

std::expected<void, physics::CollisionMeshError> World::AddCollisionMesh(const physics::CollisionMesh& mesh) {
  return impl_->map.AddCollisionMesh(mesh, kMapSurface);
}

const physics::World& World::Map() const { return impl_->map; }

void World::SetParameters(const parameters::Parameters& parameters, float tick_duration) {
  impl_->tracer_rules = TracerRulesOf(parameters, tick_duration);
  impl_->ads_field_of_view = parameters.rifle.ads_field_of_view;
}

void World::SetCharacterEye(const std::string& character, const math::Vec3& eye) {
  impl_->character_eyes.insert_or_assign(character, eye);
}

State World::RunFrame(const FrameInput& input) {
  impl_->input = &input;
  impl_->ecs.progress();
  impl_->input = nullptr;
  // Handed over, not copied: OnCommit sets every State field anew each frame,
  // so a field it stopped setting would come back moved-from.
  return std::move(impl_->frame_state);
}

}  // namespace augusta::presentation
