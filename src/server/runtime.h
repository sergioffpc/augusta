#ifndef AUGUSTA_RUNTIME_H_
#define AUGUSTA_RUNTIME_H_

#include <memory>
#include <string>
#include <vector>

#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"

// augusta::runtime is ServerRuntime (ARCHITECTURE.md §5): the augustad
// executable's own orchestrator, owning the single authoritative
// SimulationWorld and the two fixed threads ADR-0005 assigns the server
// - Simulation and Network I/O; headless, so no render thread the way
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
// Like its client-side counterpart, this currently only compiles - it
// isn't yet called from main.cpp. networking::Server (ADR-0003) and
// physics::World (ADR-0002, constructed inside simulation::World) are
// both real now (see networking.cpp, physics.cpp); ballistics::World and
// scripting::Engine (also constructed inside simulation::World) are
// still placeholder stubs. Revisit main.cpp once those land too.
namespace augusta::runtime {

// Everything ServerRuntime needs to construct SimulationWorld and start
// listening.
struct Config {
  // The Simulation thread's fixed tick rate in Hz (NFR-01 asks it to sustain
  // 60 Hz, no missed ticks), which each client is told when it joins.
  float tick_rate_hz = 0.0F;
  // What the simulation runs on and each client is told when it joins: every
  // player body's stamina rules (physics::World, shared with PredictionWorld
  // client-side).
  parameters::Parameters parameters;
  // Lua game-policy script to load (scripting::Engine, inside
  // SimulationWorld's Scripts/Behaviours phase).
  std::string script_path;
  // Local address to listen on (US-01).
  networking::Endpoint listen;
  // The map's collision, built by augusta::map from the server pack by the
  // caller: where content comes from is the executable's business.
  std::vector<physics::CollisionMesh> collision;
  // Where joining players spawn, in the order they take them, from the same pack.
  std::vector<math::Vec3> spawn_points;
};

// Owns the one authoritative SimulationWorld and the two fixed threads
// ADR-0005 assigns them to. The server process constructs exactly one,
// on what becomes the Simulation thread (see Run()).
class ServerRuntime {
 public:
  // Constructs SimulationWorld (throws whatever scripting::Engine's
  // constructor throws if script_path fails to load - see
  // simulation.h) and starts networking::Server listening on
  // config.listen (throws std::runtime_error if the address can't be
  // bound - see networking.h). Does not yet spawn any thread; see
  // Run().
  explicit ServerRuntime(const Config& config);

  // Run() always stops and joins the Network I/O thread it spawned
  // before returning, including if the Simulation loop exits via an
  // exception - so there is nothing left for this destructor to do
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
  // Authoritative State onward - until Stop() is called. Always stops
  // and joins the Network I/O thread before returning or propagating an
  // exception (see ~ServerRuntime). Must not be called more than once.
  void Run();

  // Signals Run()'s Simulation loop to stop after its current tick.
  // Safe to call from any thread - e.g. main() installing a SIGINT/
  // SIGTERM handler that calls this.
  void Stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::runtime

#endif  // AUGUSTA_RUNTIME_H_
