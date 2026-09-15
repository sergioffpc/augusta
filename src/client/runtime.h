#ifndef AUGUSTA_RUNTIME_H_
#define AUGUSTA_RUNTIME_H_

#include <memory>

#include "augusta/audio.h"
#include "augusta/input.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/presentation.h"
#include "augusta/renderer.h"

// augusta::runtime is ClientRuntime (ARCHITECTURE.md §5): the augustac
// executable's own orchestrator, owning one of every client module and
// tying them into the three-thread model ADR-0005 mandates - Main/
// Render, Simulation, and Network I/O - carrying augusta::prediction's
// per-tick Prediction State across to augusta::presentation each frame.
// Unlike the modules it composes, this one belongs to src/client - it's
// the executable's own private wiring, not a reusable engine module
// (nothing else links against it, and it has no counterpart in Shared
// Core or on the server side). See ARCHITECTURE.md §5 for the full
// Input/Networking/ClientRuntime/Renderer/Audio diagram this class
// implements; this header doesn't redraw it.
//
// What ClientRuntime does NOT yet do: turn a Command into wire bytes to
// send, or turn received bytes back into an authoritative
// physics::BodyState to reconcile against - the Networking Protocol
// (ADR-0007, custom binary format) isn't designed yet (see
// networking.h's own note on this). Until it is, the Network I/O thread
// pumps the connection but has nothing meaningful to decode, and the
// Simulation thread always reconciles against std::nullopt (see
// prediction::World::Tick).
//
// This currently only compiles - it isn't yet called from main.cpp,
// because Renderer, audio::Engine, networking::Client, and
// physics::World (constructed inside prediction::World) have no
// implementation behind their declarations yet (see e.g. renderer.h,
// audio.h); constructing a ClientRuntime today would fail to link.
// Revisit main.cpp once those land.
namespace augusta::runtime {

// Everything ClientRuntime needs to construct its owned sub-worlds/
// modules.
struct Config {
  renderer::Config renderer;
  input::Config input;
  // Every player body's stamina rules (physics::World, shared by
  // PredictionWorld here and SimulationWorld server-side).
  physics::StaminaConfig stamina;
  // The dedicated server to connect to (US-01).
  networking::Endpoint server;
  // Simulation thread's fixed tick rate, in Hz. Defaults to NFR-01's
  // server tick rate (>= 60 Hz, REQUIREMENTS.md) - PredictionWorld
  // ticking at a different rate than the server it predicts against
  // would only make reconciliation (ADR-0004) harder to reason about.
  float tick_rate_hz = 60.0F;
};

// Owns one of every client-only module/World and the three fixed
// threads ADR-0005 assigns them to. The client process constructs
// exactly one, on what becomes the Main/Render thread (see Run()).
class ClientRuntime {
 public:
  // Constructs every owned module - Input, the one audio::Engine,
  // networking::Client, PredictionWorld, PresentationWorld, and
  // Renderer, in that dependency order (Renderer needs Input as its
  // EventSink; PresentationWorld needs the audio::Engine reference) -
  // but does not yet connect to the server or spawn any thread; see
  // Run(). Throws whatever the underlying module constructors throw
  // (Renderer and audio::Engine both throw std::runtime_error on
  // device/window failure - see their own headers).
  //
  // augusta::networking::Init() must already have been called once,
  // process-wide, before this constructor runs (see networking.h) -
  // ClientRuntime doesn't call it itself since Init() is a one-time
  // process concern, not a per-instance one.
  explicit ClientRuntime(const Config& config);

  // Run() always stops and joins the Simulation and Network I/O
  // threads it spawned before returning, including if the Main/Render
  // loop exits via an exception - so there is nothing left for this
  // destructor to do once Run() has run. Also safe if Run() was never
  // called (nothing was ever spawned).
  ~ClientRuntime();

  // Non-copyable (owns unique hardware resources - the window/GPU
  // device, the audio device, the network connection) and, unlike the
  // World classes it owns, not movable either: there's no scenario
  // where relocating the one ClientRuntime mid-lifetime makes sense,
  // since Run() ties it to the thread that constructed it.
  ClientRuntime(const ClientRuntime&) = delete;
  ClientRuntime& operator=(const ClientRuntime&) = delete;
  ClientRuntime(ClientRuntime&&) = delete;
  ClientRuntime& operator=(ClientRuntime&&) = delete;

  // Spawns the Simulation and Network I/O threads (ADR-0005), then runs
  // the Main/Render loop on the calling thread - PumpEvents, read the
  // latest committed Prediction State, PresentationWorld::RunFrame,
  // Renderer::RenderFrame - until Renderer::ShouldClose() returns true.
  // Always stops and joins both spawned threads before returning or
  // propagating an exception (see ~ClientRuntime). Must be called from
  // the same thread that constructed this ClientRuntime (ADR-0009's
  // window-thread-affinity requirement, inherited from Renderer) and
  // must not be called more than once.
  void Run();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::runtime

#endif  // AUGUSTA_RUNTIME_H_
