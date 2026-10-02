#ifndef AUGUSTA_SERVER_RUNTIME_H_
#define AUGUSTA_SERVER_RUNTIME_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/supervisor.h"
#include "host.h"

// ServerRuntime (ARCHITECTURE.md §5) is the augustad executable's own
// orchestrator, owning the single authoritative SimulationWorld and the two
// fixed threads ADR-0005 assigns the server - Simulation and Network I/O; headless, so no render thread the way
// the client's counterpart (src/client/runtime.h) has. Carries
// validated commands in from augusta::networking::Server and
// SimulationWorld's Authoritative State back out to it each tick.
//
// Decoding what clients send, admitting them, screening their commands,
// and encoding and sending each tick's Authoritative State (via
// augusta::replication) is server::Host's work (host.h); this class only
// runs Host on the two threads.
//
// Unlike the client, there's no window to signal shutdown (headless) -
// Stop() is this runtime's own explicit lifecycle control instead, e.g.
// called from a SIGINT/SIGTERM handler main() installs.
//
// Constructed and run from main.cpp. networking::Server (ADR-0003) and
// physics::World (ADR-0002, constructed inside simulation::World) are
// real (see networking.cpp, physics.cpp), as is ballistics::World (also
// constructed inside simulation::World), which every round a player fires
// flies in, and scripting::Engine, which runs the scenario's Game policy.
namespace augusta::server {

// Everything ServerRuntime needs to construct SimulationWorld and start
// listening.
struct RuntimeConfig {
  // The Simulation thread's fixed tick rate in Hz (NFR-01 asks it to sustain
  // 60 Hz, no missed ticks), which each client is told when it joins.
  std::uint8_t tick_rate_hz = 0;
  // What the simulation runs on and each client is told when it joins: every
  // player body's stamina rules (physics::World, shared with PredictionWorld
  // client-side).
  parameters::Parameters parameters;
  // Local address to listen on (US-01).
  networking::Endpoint listen;
};

// Owns the one authoritative SimulationWorld and the two fixed threads
// ADR-0005 assigns them to. The server process constructs exactly one,
// on what becomes the Simulation thread (see Run()).
class ServerRuntime {
 public:
  // Constructs SimulationWorld with map's collision (throws
  // std::runtime_error if a map mesh is rejected - see host.h) and the
  // scenario's Game policy, and starts networking::Server listening on
  // config.listen (throws std::runtime_error if the address can't be bound -
  // see networking.h). Does not yet spawn any thread; see Run().
  ServerRuntime(const RuntimeConfig& config, Map map, scripting::Engine policy = {});

  // Run() always stops and joins the Network I/O thread it spawned
  // before returning, so there is nothing left for this destructor to do
  // once Run() has run. Also safe if Run() was never called.
  ~ServerRuntime();

  // Non-copyable (owns the listening network socket) and not movable
  // either - Run() ties it to the thread that constructed it, the same
  // reasoning as the client's ClientRuntime.
  ServerRuntime(const ServerRuntime&) = delete;
  ServerRuntime& operator=(const ServerRuntime&) = delete;
  ServerRuntime(ServerRuntime&&) = delete;
  ServerRuntime& operator=(ServerRuntime&&) = delete;

  // Spawns the Network I/O thread (ADR-0005), then runs the fixed-rate
  // Simulation loop on the calling thread - gather this tick's latest
  // validated commands, SimulationWorld::Tick, hand the resulting
  // Authoritative State onward - until Stop() is called or either thread
  // fails on an exception, which stops the other (supervisor.h). Always stops
  // and joins the Network I/O thread before returning. Returns the failure
  // that stopped it, nullopt if Stop() did: the caller reports it and exits.
  // Must not be called more than once.
  [[nodiscard]] std::optional<supervisor::WorkerFailure> Run();

  // Signals Run()'s Simulation loop to stop after its current tick, and the
  // Network I/O thread after its current round. Safe to call from any thread
  // and from a signal handler - e.g. main() installing a SIGINT/SIGTERM handler
  // that calls this.
  void Stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_RUNTIME_H_
