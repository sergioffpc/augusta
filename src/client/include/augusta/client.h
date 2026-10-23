#ifndef AUGUSTA_CLIENT_H_
#define AUGUSTA_CLIENT_H_

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid.hpp>

#include "augusta/assets.h"
#include "augusta/client_content.h"
#include "augusta/failure.h"
#include "augusta/input.h"
#include "augusta/reenactment.h"
#if AUGUSTA_DEBUG_TOOLS
#include "augusta/client_config.h"
#endif

/// \file
/// The client Runtime, augustac's (ARCHITECTURE.md §5, ADR-0060): it owns one of
/// every client module - Input, Audio, the harness::Session, PresentationWorld,
/// the window and the Renderer that draws into it - and runs the Main/Render
/// loop: it hands the window's events to Input and, in a Development build
/// (ADR-0074), the HUD's lines (augusta::hud) to the Renderer (ADR-0059).
/// augustac's entry point runs it behind the application boundary, and the
/// end-to-end tests link it in augustac's place.
///
/// It plays nothing itself. Sending commands, receiving the server's state and
/// reconciling the prediction is harness::Session's work, and its Prediction and
/// Network I/O threads are a harness::Runner's (ADR-0005): ClientRuntime supplies
/// each tick's command from the player's input, blends the ticks it is handed
/// into render frames, and loads what the Lobby names. As a Captured player
/// (ADR-0050) each tick's command is its harness::Reenactment's instead, the
/// keyboard and mouse move nothing, and the run ends with its reenactment.
/// Where its content comes from is its caller's business (client_content.h).
namespace augusta::client {

/// What one run varies by. The window it opens is the game's own, titled
/// "augusta", under Vulkan's validation layer in a Debug build (ADR-0059).
struct RuntimeConfig {
  input::Config input;
  /// The dedicated server to connect to (US-01), "host:port" with a numeric
  /// IP: direct IP:port only, no server discovery (ARCHITECTURE.md §3).
  std::string server_address;
  /// The character to ask to play, by its name in the scenario's manifest (ADR-0042).
  std::string character;
  /// The hash of the client pack loaded, which the server checks is the one
  /// cooked with its own.
  assets::PackHash client_pack{};
  /// The capture's player to be, as a Captured player (`--reenact`, ADR-0050):
  /// it joins at its spawn with a Reenact request, as character, which must be
  /// its. nullopt to play from the player's input.
  std::optional<harness::Script> reenactment;
  /// Set, the client is a Replay viewer (ADR-0051, `augustac --replay`): it
  /// watches the capture of this name on a replay server instead of playing,
  /// and its run ends, as the window closing ends it, once the Replay has.
  std::optional<std::string> replay;
  /// The Run's ID (client::BeginRun), which the join carries (ADR-0038).
  boost::uuids::uuid run = boost::uuids::nil_uuid();
#if AUGUSTA_DEBUG_TOOLS
  /// Whether the Debug HUD is drawn at first (`debug.hud`), until the Debug
  /// index switches it; a Shipping build has none (ADR-0074).
  bool debug_hud = true;
  /// Whether the Profiling Debug panel is shown at first
  /// (`debug.profiling.panel`), until the Debug index switches it.
  bool debug_profiling_panel = false;
  /// Whether the Physics Debug panel is shown, and each of its overlays drawn,
  /// at first (`debug.physics`), until the Debug index and the panel switch
  /// them; a Replay viewer's index never lists the panel, and none of its
  /// overlays starts on.
  config::ClientPhysicsDebugConfig debug_physics{};
  /// The directory (`debug.network.pcapng`, which checked it can be written)
  /// this run writes one pcapng file into, of every message its connection
  /// sends and receives (ADR-0076); empty, the default, writes none.
  std::filesystem::path debug_network_pcapng;
#endif
};

/// The client process constructs exactly one, on what becomes the Main/Render
/// thread (see Run()).
class ClientRuntime {
 public:
  /// Constructs every owned module - Input, the one audio::Engine, the
  /// harness::Session with its PredictionWorld, PresentationWorld and
  /// Renderer, with its window, in that dependency order (PresentationWorld
  /// needs the audio::Engine reference) -
  /// but does not yet connect to the server or start any thread; see
  /// Run(). Throws whatever the underlying module constructors throw
  /// (Renderer throws std::runtime_error on device/window failure - see its
  /// own header; audio::Engine never throws, and is silent without an output
  /// device).
  ///
  /// augusta::link::Init() must already have been called once,
  /// process-wide, before this constructor runs (see link.h), as augustac's
  /// Lifecycle does (client_application.h) - ClientRuntime doesn't call it
  /// itself since Init() is a one-time process concern, not a per-instance one.
  ///
  /// Content is loaded from the client pack by the caller (see
  /// client_content.h), since where content comes from is the executable's
  /// business, not the orchestrator's. load_character is called in the Lobby for each
  /// character another player brings (ADR-0043); it must stay callable until
  /// Run() returns. Throws failure::ClassifiedFailure
  /// (failure::Code::kInvalidContent) if physics rejects a collision mesh.
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
  /// the Main/Render loop on the calling thread - the window's events into
  /// Input, in the Lobby load every other player's character and report
  /// Ready, read the latest committed Prediction State,
  /// PresentationWorld::RunFrame, the HUD's lines and Renderer::RenderFrame -
  /// until the window should close, the
  /// session fails (refused, server unreachable, connection lost), a
  /// character cannot be loaded or the Prediction or Network I/O thread
  /// fails (which stops the others), which is what it returns, classified
  /// (client_phases.h) - a thread's first cause as its supervisor recorded it:
  /// the caller reports it and exits, since there is no reconnecting. nullopt
  /// if the player closed the window, a Captured player's run left at its
  /// captured Leave or reached the capture's Match end or the server's, which
  /// it logs beside what the capture holds (ADR-0050), or a Replay viewer's
  /// Replay ended (RuntimeConfig::replay), which loads every player's character
  /// once its Match starts, as there is no Lobby to load them in.
  /// Always stops and joins both spawned threads before returning or
  /// propagating an exception (see ~ClientRuntime). Must be called from
  /// the same thread that constructed this ClientRuntime (GLFW's
  /// window-thread-affinity requirement, inherited from the window) and
  /// must not be called more than once.
  [[nodiscard]] std::optional<failure::Failure> Run();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_H_
