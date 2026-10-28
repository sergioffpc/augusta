#ifndef AUGUSTA_SERVER_H_
#define AUGUSTA_SERVER_H_

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

#include "augusta/content.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/host.h"
#include "augusta/policy.h"
#include "augusta/replay_server.h"
#include "augusta/simulation.h"

/// \file
/// The augustad executable's orchestrator (ARCHITECTURE.md §5): it runs a
/// host::Host, or in replay mode a host::ReplayServer (ADR-0051), on the
/// two threads ADR-0005 gives the server - the Network I/O thread pumps its
/// connections, the Simulation thread ticks it at the fixed rate - and has no
/// render thread, since the server is headless. Beside them
/// it serves the metrics endpoint (metrics.h, ADR-0049) on a third thread it
/// does not supervise. Decoding what clients send, admitting them, screening
/// their commands and replicating each tick is Host's work (host.h);
/// ServerRuntime only runs it. With no window to close, Stop() ends it, or the
/// process's shutdown request it watches, which augustad's SIGINT/SIGTERM
/// handler sets (shutdown_signal.h).
namespace augusta::server {

/// What a ServerRuntime runs its server with, beside the server's own config.
struct RuntimeOptions {
  /// The TCP port the metrics endpoint serves on (ADR-0049); 0, the default,
  /// takes a free one.
  std::uint16_t metrics_port = 0;
  /// A stop request that outlives the runtime, e.g. GlobalShutdownRequest
  /// (shutdown_signal.h): once it is true, Run() stops as if Stop() had been
  /// called - at once if it already is when Run() starts. Null, the default,
  /// watches none; must outlive Run().
  const std::atomic<bool>* shutdown_request = nullptr;
  /// For tests only: asked by its supervisor at each worker's creation and
  /// execution; must outlive Run().
  failure::Faults* faults = nullptr;
#if AUGUSTA_DEBUG_TOOLS
  /// In a Development build only (ADR-0074): when not empty, the directory
  /// (`debug.network.pcapng`, which checked it can be written) this run writes
  /// one pcapng file into, of every message its listening socket or endpoint
  /// sends and receives, as the server config's tap (ADR-0076).
  std::filesystem::path network_pcapng;
#endif
};

/// The server process constructs exactly one, on what becomes the Simulation
/// thread (see Run()).
class ServerRuntime {
 public:
  /// Constructs SimulationWorld with scenario's collision and the scenario's
  /// Game policy, and starts the Link's server end listening on config.listen,
  /// throwing the failure::ClassifiedFailure Host's constructor does (see
  /// host.h). Does not yet spawn any thread or start the metrics
  /// endpoint; see Run().
  ServerRuntime(const host::HostConfig& config, scenario::Scenario scenario, policy::Policy policy = {},
                const RuntimeOptions& options = {});

  /// A replay server's runtime (ADR-0051): as above, but what it runs is a
  /// ReplayServer on config, scenario and policy (see replay_server.h, which
  /// says what it throws), whose Replays each tick at config.tick_rate_hz. It
  /// captures nothing, so it has no capture to fail.
  ServerRuntime(const host::ReplayServerConfig& config, scenario::Scenario scenario, policy::PolicyMaker policy,
                const RuntimeOptions& options = {});

  /// Run() always stops and joins the Network I/O thread it spawned
  /// before returning, so there is nothing left for this destructor to do
  /// once Run() has run. Also safe if Run() was never called.
  ~ServerRuntime();

  /// Non-copyable (owns the listening network socket) and not movable
  /// either - Run() ties it to the thread that constructed it, the same
  /// reasoning as the client's ClientRuntime.
  ServerRuntime(const ServerRuntime&) = delete;
  ServerRuntime& operator=(const ServerRuntime&) = delete;
  ServerRuntime(ServerRuntime&&) = delete;
  ServerRuntime& operator=(ServerRuntime&&) = delete;

  /// Starts the metrics endpoint, whose failure to start is logged and does not
  /// stop the server (ADR-0049). Spawns the Network I/O thread (ADR-0005), then runs the fixed-rate
  /// Simulation loop on the calling thread - gather this tick's latest
  /// validated commands, SimulationWorld::Tick, hand the resulting
  /// Authoritative State onward - until Stop() is called, the shutdown
  /// request (RuntimeOptions) is set, or either thread
  /// fails, the local transport failing among the ways it can
  /// (Host::TakeTransportFailure), which stops the other (supervisor.h):
  /// neither ticks nor pumps the network again once the stop is requested.
  /// Always stops and joins the Network I/O
  /// thread before returning, and so before Host, which it uses, can go.
  /// Returns the first cause that stopped it, nullopt if a stop asked for did:
  /// the caller reports it and exits. Must not be called more than once.
  [[nodiscard]] std::optional<failure::Failure> Run();

  /// Signals Run()'s Simulation loop to stop after its current tick, and the
  /// Network I/O thread after its current round. Safe to call from any thread
  /// while the runtime lives; a signal handler, which cannot know that it
  /// does, sets RuntimeOptions::shutdown_request instead.
  void Stop();

 private:
  struct Impl;
  template <typename Served>
  struct Running;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_H_
