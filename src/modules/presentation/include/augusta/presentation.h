#ifndef AUGUSTA_PRESENTATION_H_
#define AUGUSTA_PRESENTATION_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <vector>

#include "augusta/audio.h"
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

// augusta::presentation orchestrates PresentationWorld (ADR-0024): the
// client-side ECS pipeline, run once per render frame on the Main/Render
// thread (ADR-0005) - a different thread and cadence than
// augusta::prediction's fixed-tick Simulation thread. It translates the
// fixed-tick Prediction State (augusta::prediction::State) into smooth,
// frame-rate-independent visuals/audio, through the five ordered phases
// ADR-0024 defines (see Phase below).
//
// Like augusta::simulation and augusta::prediction, World owns one Flecs
// world (ADR-0001) internally, entirely encapsulated behind Impl
// (presentation.cpp) - no flecs header leaks in here. Phase's five
// values become five dependency-chained flecs::Phase entities, each with
// one registered flecs::system that runs once per RunFrame regardless of
// matched entities - see presentation.cpp. Entity/component shapes still
// aren't designed, so a system's body is presently a stub; what each one
// will eventually do is documented on its Phase enumerator below.
//
// Camera has no C++ home of its own - ARCHITECTURE.md doesn't list it as
// a separate Shared Core/client-only module the way WeaponHandling is; it
// stays a system inside this module (see Phase::kCamera below), exposed
// through State::camera for augusta::renderer::Renderer::SetCamera to
// consume once per render frame. Animation does have its own home:
// augusta::animation::Engine (see that header) - this module owns the one
// Engine instance and calls Update from its Animation phase.
namespace augusta::presentation {

// PresentationWorld's five phases (ADR-0024), executed in this exact
// order every render frame. Neither this pipeline nor PredictionWorld
// contains a Scripts/Behaviours phase - game policy is exclusively
// server-authoritative (ADR-0024).
enum class Phase {
  // Mechanism. Shows the local player's predicted body the fraction of the
  // tick elapsed at render time of the way from the previous Prediction State
  // to the newest (BlendTicks, local_view.h), and slides it out of the jumps
  // a reconciliation replay makes (Correction, ADR-0004). For every other
  // player, buffers the newest reported body (World::RunFrame's snapshot
  // parameter) per remote entity, placed on the server's timeline by its
  // tick, and renders each kInterpolationDelay behind a render-side clock
  // aligned to that timeline, interpolated between the two surrounding
  // updates (ServerClock and RemoteInterpolator, interpolation.h) - smooth
  // motion independent of render frame rate and of when updates arrive. An
  // entity no longer in the snapshot is no longer shown, nor for the rest of
  // the match is one whose Death has arrived (FrameInput::deaths), and neither
  // is anyone while there is no snapshot (outside a match). Each is drawn as its character
  // (FrameInput::characters), turned where it faces. Which moment of the
  // server's timeline they are shown at is the frame's view (ViewAt,
  // interpolation.h). Moves the fight on to the frame's time too: starts a
  // tracer for every Shot the frame is handed and moves every tracer along its
  // trajectory (Tracers, tracers.h), and shows the muzzle flash of every other
  // player's Shot (effects.h).
  kInterpolation,
  // Mechanism. View camera position: the local player's body where
  // kInterpolation just showed it (local_position, above) plus its
  // character's eye (World's constructor, ADR-0040) for the body's stance
  // (LocalCamera, local_view.h), recomputed every frame - so the camera is
  // attached to the character and tracks wherever, and however low, the local
  // player's body actually is. Rotation is where the local player aims as of
  // this frame (FrameInput::aim), not as of the last tick, turned by its
  // rifle's predicted Recoil offset, so the view shows where the next round
  // goes; the field of view zooms in and out with ADS (AdsZoom). Also what
  // shows over the view: the crosshair from the hip, the hit marker after a Hit
  // confirmation (HitMarker), and the local player's muzzle flash on the frame
  // its predicted fire fires a round (FiredRounds). Once the local player's
  // Death has arrived it is a spectator until the match ends: the camera
  // follows the eye of a living player instead (Spectator and WatchedCamera,
  // local_view.h; fire moves it on), holds where it was while no one is left
  // alive, and neither turns with the local player's aim nor zooms; no
  // crosshair or hit marker shows. View bob is still future work. Not yet a
  // module of its own - see the header comment above.
  kCamera,
  // Mechanism. Drives skeletal/procedural animation from interpolated
  // movement and weapon state - augusta::animation::Engine::Update, once
  // per visible player character (local and remote alike, unlike
  // Interpolation/Camera which only concern the local player's own
  // predicted state).
  kAnimation,
  // Mechanism. Sets the listener to the camera Phase::kCamera just placed
  // (ListenerOf, audio_cues.h), then plays the frame's one-shot cues
  // (SelectCues, audio_cues.h) through augusta::audio::Engine: the local
  // player's own gunshot on the frame its predicted fire fires a round, every
  // other player's gunshot from its Shot's origin, and the hit marker on a Hit
  // confirmation.
  kAudioCues,
  // Mechanism. Packages the frame's presentation data into Presentation
  // State (State, below), for augusta::renderer::Renderer::RenderFrame
  // to consume.
  kCommit,
};

/// One dynamic body as the server last reported it, named by its entity - not
/// by the session of the player who controls it, if any.
struct DynamicBody {
  EntityId entity{};
  physics::BodyState state{};
  /// Where the body faces: the yaw of its player's view, in radians (command::Command).
  float yaw = 0.0F;
};

/// Every dynamic body as of one server tick: an Authoritative State update, in
/// presentation's own terms - World::RunFrame's input (see there).
struct WorldSnapshot {
  /// The server tick the bodies are from.
  tick::Tick tick = 0;
  /// The server's tick duration, in seconds (its tick rate's inverse,
  /// ADR-0039): tick × tick_duration is when the bodies are from on the
  /// server's timeline.
  double tick_duration = 0.0;
  std::vector<DynamicBody> bodies;
};

/// The body of one player in the match and that player's character index
/// (ADR-0042), as Match start named them - World::RunFrame's input, in
/// presentation's own terms (see there).
struct PlayerCharacter {
  EntityId entity{};
  std::uint8_t character = 1;
};

/// One round a player fired, as the server announced it (CONTEXT.md's Shot) -
/// World::RunFrame's input, in presentation's own terms (see there).
struct Shot {
  /// The body of the player who fired it.
  EntityId shooter{};
  /// Where the round left from: the shooter's eye.
  math::Vec3 origin{};
  /// Where it left for, as a view's yaw in radians (command::Command).
  float yaw = 0.0F;
  /// Where it left for, as a view's pitch in radians (command::Command).
  float pitch = 0.0F;
};

/// What one render frame is shown from - World::RunFrame's input. Every field is
/// presentation's own type: ClientRuntime converts them from the harness's at
/// its edge, the way each peer converts the protocol at its own (ADR-0038), so
/// this module does not depend on the network session.
struct FrameInput {
  /// The two most recently committed Prediction States and how far between them
  /// the frame is, which Interpolation blends by.
  PredictedTicks ticks{};
  /// Where the local player aims as of this frame, which the camera takes.
  Aim aim{};
  /// Whether the local player holds fire as of this frame: a spectator's press
  /// moves its view on to the next living player.
  bool fire = false;
  /// The body this client's player controls, or nullopt before its first match.
  std::optional<EntityId> local_entity;
  /// The newest Authoritative State update of the match in progress, or nullopt
  /// outside one. Every body in it other than local_entity's is fed to the
  /// RemoteInterpolator (see interpolation.h); a repeated snapshot (from no
  /// newer a tick than the previous frame's) is not recorded again.
  std::optional<WorldSnapshot> snapshot;
  /// Every player's character, as the server named them when the match
  /// started, in Session order (whom a spectator watches first and next), or
  /// empty before the first.
  std::vector<PlayerCharacter> characters;
  /// The Shots announced since the previous frame, in the order they arrived.
  std::vector<Shot> shots;
  /// How many Hit confirmations arrived since the previous frame.
  std::uint32_t hit_confirmations = 0;
  /// The bodies whose Death arrived since the previous frame, in the order
  /// they arrived: each leaves presentation for the rest of the match.
  std::vector<EntityId> deaths;
};

// PresentationWorld's per-frame output - ADR-0024/ARCHITECTURE.md's
// "Presentation State". Today it holds where the local player is shown, the
// camera, every remote player, the fight's tracers, impacts and muzzle flashes,
// and what shows over the view; later phases add what they present (animation
// poses). Cues are not in it: Phase::kAudioCues plays them itself.
struct State {
  /// Where the local player is shown: its predicted position blended between
  /// the two newest ticks, plus the offset that hides a reconciliation jump
  /// and fades (see correction.h).
  math::Vec3 local_position{};
  /// The local player's view camera this frame (Phase::kCamera) - tracks
  /// local_position at its character's eye for its stance, every frame, turned
  /// where the player looks; a spectator's, the watched player's eye.
  Camera camera{};
  /// Every other player in the match, at its interpolated position and stance
  /// this frame (RemoteInterpolator::Sample, interpolation.h), with its
  /// character. Empty before the client has received an Authoritative State,
  /// outside a match, or once alone in the match.
  std::vector<RemotePlayer> remote_players;
  /// What of the server's timeline remote_players are shown at this frame, for
  /// the Commands sampled on it to report (ADR-0044); nullopt before the client
  /// has received an Authoritative State and outside a match.
  std::optional<ShownView> view;
  /// Every tracer in flight, as drawn this frame (tracers.h).
  std::vector<Tracer> tracers;
  /// Every impact on the Map still showing.
  std::vector<Effect> impacts;
  /// Every muzzle flash still showing, the local player's and every other's.
  std::vector<Effect> muzzle_flashes;
  /// Whether the crosshair shows: from the hip, not in ADS, and never to a
  /// spectator.
  bool crosshair = true;
  /// Whether the hit marker shows (HitMarker); never to a spectator.
  bool hit_marker = false;
};

// The client's single PresentationWorld. The client constructs exactly
// one, on the Main/Render thread (ADR-0005). Owns the Flecs world it
// runs inside of (see header comment); no I/O happens inside RunFrame
// beyond the AudioCues phase's calls into audio_engine (ARCHITECTURE.md
// §8's "no I/O inside ECS worlds" is about device/network/file I/O, not
// about the audio/render boundary components this pipeline exists to
// drive).
//
// Move-only: copying would either duplicate or alias the owned Flecs
// world, neither of which is meaningful.
class World {
 public:
  // audio_engine must outlive this World - the same reference-not-owned
  // pattern as augusta::renderer::Renderer's input_sink parameter:
  // ClientRuntime (src/client/runtime.h) constructs the client's one
  // audio::Engine and wires it to this World's AudioCues phase, which plays
  // cue_sounds, loaded into audio_engine here. eye is the local player's character's eye
  // standing, in that character's root space (its feet at the origin,
  // ADR-0040): where Phase::kCamera puts the camera relative to the predicted
  // body, lowered for the body's stance (LocalCamera, local_view.h). Also
  // registers Phase's five phases and their systems on the owned Flecs world
  // (see header comment). Tracers meet no Map until AddCollisionMesh adds it.
  World(audio::Engine& audio_engine, const audio::CueSounds& cue_sounds, const math::Vec3& eye);
  ~World();

  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept;
  World& operator=(World&&) noexcept;

  /// Adds immovable level geometry for tracers to meet, the same Map the
  /// server's bullets meet.
  std::expected<void, physics::CollisionMeshError> AddCollisionMesh(const physics::CollisionMesh& mesh);

  /// Takes the parameters the server sent when it admitted this client and its
  /// tick duration, in seconds: what a Shot's tracer flies by, and the ADS field
  /// of view. Until then no tracer is drawn and ADS does not zoom.
  void SetParameters(const parameters::Parameters& parameters, float tick_duration);

  /// Takes the eye standing of the character with index character (ADR-0040,
  /// ADR-0042), in its root space: where a spectator's camera sits watching a
  /// player of that character. A character whose eye was never set is watched
  /// from the local player's character's eye.
  void SetCharacterEye(std::uint8_t character, const math::Vec3& eye);

  // Runs all five Phase values above, in their declared order, for one render
  // frame (internally, one flecs::world::progress() call), shown from input.
  // Returns the frame's Presentation State.
  State RunFrame(const FrameInput& input);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_PRESENTATION_H_
