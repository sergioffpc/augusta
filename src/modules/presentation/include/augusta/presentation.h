#ifndef AUGUSTA_PRESENTATION_H_
#define AUGUSTA_PRESENTATION_H_

#include <memory>

#include "augusta/audio.h"
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
// Camera has no C++ home of its own yet - ARCHITECTURE.md doesn't list
// it as a separate Shared Core/client-only module the way WeaponHandling
// is; it's expected to stay a system inside this module. Animation does
// have one now: augusta::animation::Engine (see that header) - this
// module owns the one Engine instance and calls Update from its
// Animation phase. What augusta::renderer::Renderer::RenderFrame
// actually draws from the resulting Presentation State is still
// deliberately undesigned (see renderer.h) - revisit both headers
// together once that lands.
namespace augusta::presentation {

// PresentationWorld's five phases (ADR-0024), executed in this exact
// order every render frame. Neither this pipeline nor PredictionWorld
// contains a Scripts/Behaviours phase - game policy is exclusively
// server-authoritative (ADR-0024).
enum class Phase {
  // Mechanism. Interpolates between the last two Prediction States for
  // smooth motion at render frame rate - World::RunFrame's latest
  // parameter and the previous call's, internally retained.
  kInterpolation,
  // Mechanism. View camera - position/orientation, ADS zoom transition,
  // recoil kick decay, view bob. Not yet a module of its own - see the
  // header comment above.
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

// PresentationWorld's per-frame output - ADR-0024/ARCHITECTURE.md's
// "Presentation State". Deliberately empty for now - same
// deferred-design posture as augusta::renderer's "what gets drawn"
// (renderer.h) and augusta::prediction::State; its real shape depends on
// ECS component shapes not yet designed.
struct State {};

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
  // pattern as augusta::renderer::Renderer's input_sink parameter, since
  // ClientRuntime (not yet designed) is expected to construct the
  // client's one audio::Engine and wire it to both Renderer's window and
  // this World's AudioCues phase. Also registers Phase's five phases and
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
  // from and uses latest directly. Returns the frame's Presentation
  // State.
  State RunFrame(const prediction::State& latest);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_PRESENTATION_H_
