#ifndef AUGUSTA_SERVER_HOST_H_
#define AUGUSTA_SERVER_HOST_H_

#include <memory>
#include <string>

#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"

// augusta::server::Host is the server's network boundary and the
// authoritative SimulationWorld (ADR-0023) without the threads and the clock:
// ServerRuntime (src/server) runs PumpNetwork on the Network I/O thread and
// Tick on the Simulation thread at a fixed rate (ADR-0005), while a test calls
// both by hand, so a match can be driven tick by tick with no sleeping.
//
// The Network I/O thread's PumpNetwork and the Simulation thread's Tick may
// run concurrently: what they share (the latest commands) is guarded inside.
namespace augusta::server {

/// Everything a Host needs to construct SimulationWorld and start listening.
struct HostConfig {
  /// Every player body's stamina rules (physics::World, shared with PredictionWorld).
  physics::StaminaConfig stamina{};
  /// Lua game-policy script for SimulationWorld's Scripts/Behaviours phase.
  std::string script_path{};
  /// Local address to listen on (US-01).
  networking::Endpoint listen{};
};

/// The server's listening socket and its SimulationWorld, without threads or a clock.
class Host {
 public:
  /// Constructs SimulationWorld (throws what its scripting engine throws if
  /// script_path fails to load) and starts listening (throws
  /// std::runtime_error if the address can't be bound).
  explicit Host(const HostConfig& config);
  ~Host();

  // Not copyable or movable: owns the listening socket.
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;
  Host(Host&&) = delete;
  Host& operator=(Host&&) = delete;

  /// Does one round of the Network I/O thread's work: connection events and received messages.
  void PumpNetwork();

  /// Runs one fixed tick of SimulationWorld on the latest commands and returns its state.
  simulation::State Tick(float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HOST_H_
