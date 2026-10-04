#ifndef AUGUSTA_CLIENT_RUNTIME_H_
#define AUGUSTA_CLIENT_RUNTIME_H_

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "augusta/assets.h"
#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/networking.h"
#include "augusta/renderer.h"
#include "augusta/supervisor.h"
#include "character_loader.h"
#include "content.h"

/// \file
/// The augustac executable's orchestrator (ARCHITECTURE.md §5): it owns one of
/// every client module - Input, Audio, the harness::Session, PresentationWorld
/// and Renderer - and runs the Main/Render loop. It belongs to src/client, not
/// to the engine's modules: nothing else links against it.
///
/// It plays nothing itself. Sending commands, receiving the server's state and
/// reconciling the prediction is harness::Session's work, and its Prediction and
/// Network I/O threads are a harness::Runner's (ADR-0005): ClientRuntime supplies
/// each tick's command from the player's input, blends the ticks it is handed
/// into render frames, and loads what the Lobby names. Where its content comes
/// from is main.cpp's business (content.h).
namespace augusta::client {

struct RuntimeConfig {
  renderer::Config renderer;
  input::Config input;
  /// The dedicated server to connect to (US-01).
  networking::Endpoint server;
  /// The character to ask to play, by its path relative to `authoring/` (ADR-0042).
  std::string character;
  /// The hash of the client pack loaded, which the server checks is the one
  /// cooked with its own.
  assets::PackHash client_pack{};
};

/// Why Run() stopped without the player closing the window: the session ended
/// on its own, a character could not be loaded, or the Prediction or Network I/O
/// thread stopped on an exception (for one, the transport rejecting the server
/// address).
using RunFailure = std::variant<harness::Failure, CharacterError, supervisor::WorkerFailure>;

/// What to tell whoever runs the process about why the client stopped.
[[nodiscard]] std::string DescribeRunFailure(const RunFailure& failure);

/// The client process constructs exactly one, on what becomes the Main/Render
/// thread (see Run()).
class ClientRuntime {
 public:
  /// Constructs every owned module - Input, the one audio::Engine, the
  /// harness::Session with its PredictionWorld, PresentationWorld and
  /// Renderer, in that dependency order (Renderer needs Input as its
  /// EventSink; PresentationWorld needs the audio::Engine reference) -
  /// but does not yet connect to the server or start any thread; see
  /// Run(). Throws whatever the underlying module constructors throw
  /// (Renderer throws std::runtime_error on device/window failure - see its
  /// own header; audio::Engine never throws, and is silent without an output
  /// device).
  ///
  /// augusta::networking::Init() must already have been called once,
  /// process-wide, before this constructor runs (see networking.h) -
  /// ClientRuntime doesn't call it itself since Init() is a one-time
  /// process concern, not a per-instance one.
  ///
  /// Content is loaded from the client pack by the caller (see content.h),
  /// since where content comes from is the executable's business, not the
  /// orchestrator's. load_character is called in the Lobby for each
  /// character another player brings (ADR-0043); it must stay callable until
  /// Run() returns. Throws std::runtime_error if physics rejects a collision
  /// mesh.
  ClientRuntime(const RuntimeConfig& config, Content content);

  /// Run() always stops and joins the Prediction and Network I/O
  /// threads it started before returning, including if the Main/Render
  /// loop exits via an exception - so there is nothing left for this
  /// destructor to do once Run() has run. Also safe if Run() was never
  /// called (nothing was ever started).
  ~ClientRuntime();

  /// Non-copyable (owns unique hardware resources - the window/GPU
  /// device, the audio device, the network connection) and, unlike the
  /// World classes it owns, not movable either: there's no scenario
  /// where relocating the one ClientRuntime mid-lifetime makes sense,
  /// since Run() ties it to the thread that constructed it.
  ClientRuntime(const ClientRuntime&) = delete;
  ClientRuntime& operator=(const ClientRuntime&) = delete;
  ClientRuntime(ClientRuntime&&) = delete;
  ClientRuntime& operator=(ClientRuntime&&) = delete;

  /// Starts the Prediction and Network I/O threads (ADR-0005) under a
  /// harness::Runner, waits for the server to admit this client, then runs
  /// the Main/Render loop on the calling thread - PumpEvents, in the Lobby load
  /// every other player's character and report Ready, read the latest
  /// committed Prediction State, PresentationWorld::RunFrame,
  /// Renderer::RenderFrame - until Renderer::ShouldClose() returns true, the
  /// session fails (refused, server unreachable, connection lost), a
  /// character cannot be loaded or the Prediction or Network I/O thread
  /// throws (which stops the others), which is what it returns: the caller
  /// reports it and exits, since there is no reconnecting. nullopt if the
  /// player closed the window.
  /// Always stops and joins both spawned threads before returning or
  /// propagating an exception (see ~ClientRuntime). Must be called from
  /// the same thread that constructed this ClientRuntime (ADR-0009's
  /// window-thread-affinity requirement, inherited from Renderer) and
  /// must not be called more than once.
  [[nodiscard]] std::optional<RunFailure> Run();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_RUNTIME_H_
