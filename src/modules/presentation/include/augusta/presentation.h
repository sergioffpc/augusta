#ifndef AUGUSTA_PRESENTATION_H_
#define AUGUSTA_PRESENTATION_H_

#include <memory>
#include <optional>
#include <vector>

#include "augusta/audio.h"
#include "augusta/harness.h"
#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/prediction.h"
#include "augusta/protocol.h"

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
  // Mechanism. Slides the local player out of the jumps a reconciliation
  // replay makes (Correction, ADR-0004). For every other player, buffers the
  // newest Authoritative State (World::RunFrame's authoritative parameter)
  // per remote session and renders each kInterpolationDelay behind the
  // newest update, interpolated between the two surrounding updates
  // (RemoteInterpolator, interpolation.h) - smooth motion independent of
  // render frame rate. A session no longer in authoritative's player list is
  // no longer shown, and neither is anyone while there is no Authoritative
  // State (outside a match). Each is drawn as its character from Match start.
  kInterpolation,
  // Mechanism. View camera position: local_body's predicted position (same
  // one kInterpolation just offset for local_position, above) plus a fixed
  // eye-height offset, recomputed every frame - so the camera tracks
  // wherever the local player's body actually is, instead of the one-shot
  // placement scene_loader.cpp used to freeze it at. Rotation is the local
  // player's view (World::RunFrame's view_rotation). ADS zoom transition,
  // recoil kick decay, and view bob are still future work. Not yet a module
  // of its own - see the header comment above.
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

// The local player's view camera for one frame - Phase::kCamera's output.
// A plain position/rotation pair, not augusta::renderer::Camera itself: this
// module stays decoupled from augusta_renderer the same way
// presentation::RemotePlayer does (see renderer::RemotePlayer's doc comment)
// - ClientRuntime's ToRenderer maps both into their renderer-side
// equivalents.
struct Camera {
  math::Vec3 position{};
  math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
};

// PresentationWorld's per-frame output - ADR-0024/ARCHITECTURE.md's
// "Presentation State". Beyond local_position, camera, and remote_players,
// deliberately empty for now - same deferred-design posture as
// augusta::renderer's "what gets drawn" (renderer.h) and
// augusta::prediction::State; its real shape depends on ECS component
// shapes not yet designed.
struct State {
  /// Where the local player is shown: its predicted position, plus the offset
  /// that hides a reconciliation jump and fades (see correction.h).
  math::Vec3 local_position{};
  /// The local player's view camera this frame (Phase::kCamera) - tracks
  /// local_position at eye height, every frame, turned where the player looks.
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
  // World's AudioCues phase. Also registers Phase's five phases and
  // their systems on the owned Flecs world (see header comment).
  explicit World(audio::Engine& audio_engine);
  ~World();

  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept;
  World& operator=(World&&) noexcept;

  // Runs all five Phase values above, in their declared order, for one
  // render frame (internally, one flecs::world::progress() call). latest
  // is the most recently committed prediction::State; Interpolation
  // blends it against the previous call's latest, internally retained -
  // the first call after construction has no previous state to blend
  // from and uses latest directly. view_rotation is where the local player
  // looks (input::ViewRotation of its latest Command), which the camera
  // takes as its rotation. local_session is this client's own
  // session, or nullopt before the server has admitted it; authoritative is
  // the newest Authoritative State of the match in progress, or nullopt
  // outside one, and match_start what the server said when it started, which
  // names each player's character - all harness::Session getters
  // (GetSessionId, GetAuthoritativeState, GetMatchStart). Every player in
  // authoritative other than local_session is fed to this World's
  // RemoteInterpolator (see interpolation.h); a repeated authoritative (same
  // tick as the previous call) is not recorded again. Returns the frame's
  // Presentation State.
  State RunFrame(const prediction::State& latest, const math::Quat& view_rotation,
                 std::optional<protocol::SessionIdWire> local_session,
                 const std::optional<harness::AuthoritativeState>& authoritative,
                 const std::optional<harness::MatchStart>& match_start);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_PRESENTATION_H_
