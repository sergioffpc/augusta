#ifndef AUGUSTA_PRESENTATION_H_
#define AUGUSTA_PRESENTATION_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "augusta/audio.h"
#include "augusta/interpolation.h"
#include "augusta/local_view.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"

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
  // entity no longer in the snapshot is no longer shown, and neither is anyone
  // while there is no snapshot (outside a match). Each is drawn as its character
  // (World::RunFrame's characters parameter).
  kInterpolation,
  // Mechanism. View camera position: the local player's body where
  // kInterpolation just showed it (local_position, above) plus its
  // character's eye (World's constructor, ADR-0040) for the body's stance
  // (EyeAt, local_view.h), recomputed every frame - so the camera is attached
  // to the character and tracks wherever, and however low, the local
  // player's body actually is. Rotation is where the local player looks as of
  // this frame (World::RunFrame's view_rotation), not as of the last tick.
  // ADS zoom transition, recoil kick decay, and view bob are still future
  // work. Not yet a module of its own - see the header comment above.
  kCamera,
  // Mechanism. Drives skeletal/procedural animation from interpolated
  // movement and weapon state - augusta::animation::Engine::Update, once
  // per visible player character (local and remote alike, unlike
  // Interpolation/Camera which only concern the local player's own
  // predicted state).
  kAnimation,
  // Mechanism. Translates events carried in the Prediction State (e.g.
  // fire, footstep) into spatialized audio cues -
  // augusta::audio::Engine::SetListener then PlaySound, per audio.h's
  // own note that this phase is what calls them.
  kAudioCues,
  // Mechanism. Packages the frame's presentation data into Presentation
  // State (State, below), for augusta::renderer::Renderer::RenderFrame
  // and augusta::audio to consume.
  kCommit,
};

/// One dynamic body as the server last reported it, named by its entity - not
/// by the session of the player who controls it, if any.
struct DynamicBody {
  EntityId entity{};
  physics::BodyState state{};
};

/// Every dynamic body as of one server tick: an Authoritative State update, in
/// presentation's own terms - World::RunFrame's input (see there).
struct WorldSnapshot {
  /// The server tick the bodies are from.
  std::uint32_t tick = 0;
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

// PresentationWorld's per-frame output - ADR-0024/ARCHITECTURE.md's
// "Presentation State". Beyond local_position, camera, and remote_players,
// deliberately empty for now - same deferred-design posture as
// augusta::renderer's "what gets drawn" (renderer.h) and
// augusta::prediction::State; its real shape depends on ECS component
// shapes not yet designed.
struct State {
  /// Where the local player is shown: its predicted position blended between
  /// the two newest ticks, plus the offset that hides a reconciliation jump
  /// and fades (see correction.h).
  math::Vec3 local_position{};
  /// The local player's view camera this frame (Phase::kCamera) - tracks
  /// local_position at its character's eye for its stance, every frame, turned
  /// where the player looks.
  Camera camera{};
  /// Every other player in the match, at its interpolated position and stance
  /// this frame (RemoteInterpolator::Sample, interpolation.h), with its
  /// character. Empty before the client has received an Authoritative State,
  /// outside a match, or once alone in the match.
  std::vector<RemotePlayer> remote_players;
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
  // audio::Engine and wires it to both Renderer's window and this
  // World's AudioCues phase. eye is the local player's character's eye
  // standing, in that character's root space (its feet at the origin,
  // ADR-0040): where Phase::kCamera puts the camera relative to the predicted
  // body, lowered for the body's stance (EyeAt, local_view.h). Also
  // registers Phase's five phases and their systems on the owned Flecs world
  // (see header comment).
  World(audio::Engine& audio_engine, const math::Vec3& eye);
  ~World();

  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept;
  World& operator=(World&&) noexcept;

  // Runs all five Phase values above, in their declared order, for one
  // render frame (internally, one flecs::world::progress() call). ticks
  // are the two most recently committed prediction::State values and how
  // far between them the frame is, which Interpolation blends by.
  // view_rotation is where the local player looks as of this frame
  // (input::Input::CurrentView - newer than its latest Command's), which the
  // camera takes as its rotation. local_entity is the body this client's
  // player controls, or nullopt before its first match; snapshot is the
  // newest Authoritative State update of the match in progress, or nullopt
  // outside one; and characters is every player's character, as the server
  // named them when the match started, or empty before the first. All three
  // are presentation's own types: ClientRuntime converts them from the
  // harness's at its edge, the way each peer converts the protocol at its own
  // (ADR-0038), so this module does not depend on the network session. Every
  // body in snapshot other than local_entity's is fed to this World's
  // RemoteInterpolator (see interpolation.h); a repeated snapshot (from no
  // newer a tick than the previous call's) is not recorded again. Returns the
  // frame's Presentation State.
  State RunFrame(const PredictedTicks& ticks, const math::Quat& view_rotation, std::optional<EntityId> local_entity,
                 const std::optional<WorldSnapshot>& snapshot, std::span<const PlayerCharacter> characters);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_PRESENTATION_H_
