#ifndef AUGUSTA_SERVER_RUNTIME_H_
#define AUGUSTA_SERVER_RUNTIME_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "content.h"
#include "host.h"
#include "replay_server.h"

/// \file
/// The augustad executable's orchestrator (ARCHITECTURE.md §5): it runs a
/// server::Host, or in replay mode a server::ReplayServer (ADR-0051), on the
/// two threads ADR-0005 gives the server - the Network I/O thread pumps its
/// connections, the Simulation thread ticks it at the fixed rate - and has no
/// render thread, since the server is headless. Beside them
/// it serves the metrics endpoint (metrics.h, ADR-0049) on a third thread it
/// does not supervise. Decoding what clients send, admitting them, screening
/// their commands and replicating each tick is Host's work (host.h);
/// ServerRuntime only runs it. With no window to close, Stop() ends it, e.g.
/// from the SIGINT/SIGTERM handler main.cpp installs.
namespace augusta::server {

/// The server process constructs exactly one, on what becomes the Simulation
/// thread (see Run()).
class ServerRuntime {
 public:
  /// Constructs SimulationWorld with scenario's collision and the scenario's
  /// Game policy, and starts networking::Server listening on config.listen,
  /// throwing the failure::ClassifiedFailure Host's constructor does (see
  /// host.h). Does not yet spawn any thread or start the metrics
  /// endpoint; see Run(). faults, for tests only, is asked by its supervisor
  /// at each worker's creation and execution, and must outlive Run().
  ServerRuntime(const HostConfig& config, std::uint16_t metrics_port, Scenario scenario, scripting::Engine policy = {},
                failure::Faults* faults = nullptr);

  /// A replay server's runtime (ADR-0051): as above, but what it runs is a
  /// ReplayServer on config, scenario and policy (see replay_server.h, which
  /// says what it throws), whose Replays each tick at config.tick_rate_hz. It
  /// captures nothing, so it has no capture to fail.
  ServerRuntime(const ReplayServerConfig& config, std::uint16_t metrics_port, Scenario scenario, PolicyMaker policy,
                failure::Faults* faults = nullptr);

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
  /// Authoritative State onward - until Stop() is called or either thread
  /// fails, the local transport failing among the ways it can
  /// (Host::TakeTransportFailure), which stops the other (supervisor.h):
  /// neither ticks nor pumps the network again once the stop is requested.
  /// Always stops and joins the Network I/O
  /// thread before returning, and so before Host, which it uses, can go.
  /// Returns the first cause that stopped it, nullopt if Stop() did:
  /// the caller reports it and exits. Must not be called more than once.
  [[nodiscard]] std::optional<failure::Failure> Run();

  /// Signals Run()'s Simulation loop to stop after its current tick, and the
  /// Network I/O thread after its current round. Safe to call from any thread
  /// and from a signal handler - e.g. main() installing a SIGINT/SIGTERM handler
  /// that calls this.
  void Stop();

 private:
  struct Impl;
  template <typename Served>
  struct Running;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_RUNTIME_H_
