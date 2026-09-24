#ifndef AUGUSTA_RUNTIME_H_
#define AUGUSTA_RUNTIME_H_

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "augusta/audio.h"
#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/presentation.h"
#include "augusta/protocol.h"
#include "augusta/renderer.h"
#include "scene_loader.h"

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
// Sending commands, receiving authoritative state and reconciling the
// prediction against it is harness::Session's work (see harness.h); the
// Prediction thread only hands it each tick's command, and the Network I/O
// thread only pumps it.
//
// Constructed and run from main.cpp today. Renderer (ADR-0009, the M1
// Falcor spike), networking::Client (ADR-0003), and physics::World
// (ADR-0002, constructed inside prediction::World) are all real -
// audio::Engine is still a placeholder stub (see audio.h/audio.cpp),
// which is why there's no sound yet; a stub still links and runs as a
// no-op, same as every other module before its real implementation
// landed (see e.g. physics.h's own history).
namespace augusta::runtime {

// Everything ClientRuntime needs to construct its owned sub-worlds/
// modules.
struct Config {
  renderer::Config renderer;
  input::Config input;
  // The dedicated server to connect to (US-01).
  networking::Endpoint server;
  // The character to ask to play, by its path relative to `authoring/` (ADR-0042).
  std::string character;
  // The hash of the client pack loaded, which the server checks is the one
  // cooked with its own.
  protocol::PackHash client_pack{};
};

// The map's collision, built by augusta::map from the client pack by the
// caller: where content comes from is the executable's business, not the
// config file's - so it travels alongside Config rather than inside it.
struct Map {
  std::vector<physics::CollisionMesh> collision;
};

// Loads the visual mesh of the character with the given index from the client
// pack (client::LoadCharacterMesh), or says why it could not.
using CharacterMeshLoader = std::function<std::expected<renderer::SceneMesh, client::SceneError>(std::uint8_t)>;

// Why Run() stopped without the player closing the window: the session ended
// on its own, or a character's mesh could not be loaded.
using Failure = std::variant<harness::Failure, client::SceneError>;

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
  //
  // scene is what the Renderer draws every frame and map is what physics
  // ticks against, both loaded from the client pack by the caller (see
  // scene_loader.h and map.h), since where content comes from is the
  // executable's business, not the orchestrator's. For the same reason the
  // caller hands in load_character_mesh, which Run() calls in the Lobby for
  // each character another player brings (ADR-0043); it must stay callable
  // until Run() returns. Throws std::runtime_error if physics rejects a
  // collision mesh.
  ClientRuntime(const Config& config, Map map, const renderer::Scene& scene, CharacterMeshLoader load_character_mesh);

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
  // the Main/Render loop on the calling thread - PumpEvents, in the Lobby load
  // every other player's character and report Ready, read the latest
  // committed Prediction State, PresentationWorld::RunFrame,
  // Renderer::RenderFrame - until Renderer::ShouldClose() returns true, the
  // session fails (refused, server unreachable, connection lost) or a
  // character's mesh cannot be loaded, which is what it returns: the caller
  // reports it and exits, since there is no reconnecting. nullopt if the
  // player closed the window.
  // Always stops and joins both spawned threads before returning or
  // propagating an exception (see ~ClientRuntime). Must be called from
  // the same thread that constructed this ClientRuntime (ADR-0009's
  // window-thread-affinity requirement, inherited from Renderer) and
  // must not be called more than once.
  [[nodiscard]] std::optional<Failure> Run();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::runtime

#endif  // AUGUSTA_RUNTIME_H_
