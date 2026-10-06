#ifndef AUGUSTA_SERVER_RUNTIME_H_
#define AUGUSTA_SERVER_RUNTIME_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/supervisor.h"
#include "host.h"

/// \file
/// The augustad executable's orchestrator (ARCHITECTURE.md §5): it runs a
/// server::Host on the two threads ADR-0005 gives the server - the Network I/O
/// thread pumps its connections, the Simulation thread ticks it at the fixed
/// rate - and has no render thread, since the server is headless. Beside them
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
  /// Constructs SimulationWorld with scenario's collision (throws
  /// std::runtime_error if a map mesh is rejected - see host.h) and the
  /// scenario's Game policy, and starts networking::Server listening on
  /// config.listen (throws std::runtime_error if the address can't be bound -
  /// see networking.h). Does not yet spawn any thread or start the metrics
  /// endpoint; see Run().
  ServerRuntime(const HostConfig& config, std::uint16_t metrics_port, Scenario scenario, scripting::Engine policy = {});

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
  /// fails on an exception, which stops the other (supervisor.h). Always stops
  /// and joins the Network I/O thread before returning. Returns the failure
  /// that stopped it, nullopt if Stop() did: the caller reports it and exits.
  /// Must not be called more than once.
  [[nodiscard]] std::optional<supervisor::WorkerFailure> Run();

  /// Signals Run()'s Simulation loop to stop after its current tick, and the
  /// Network I/O thread after its current round. Safe to call from any thread
  /// and from a signal handler - e.g. main() installing a SIGINT/SIGTERM handler
  /// that calls this.
  void Stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_RUNTIME_H_
