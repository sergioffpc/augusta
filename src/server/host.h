#ifndef AUGUSTA_SERVER_HOST_H_
#define AUGUSTA_SERVER_HOST_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"
#include "parameters_loader.h"

// augusta::server::Host is the server's network boundary and the
// authoritative SimulationWorld (ADR-0023) without the threads and the clock:
// ServerRuntime (src/server) runs PumpNetwork on the Network I/O thread and
// Tick on the Simulation thread at a fixed rate (ADR-0005), while a test calls
// both by hand, so a match can be driven tick by tick with no sleeping.
//
// The Network I/O thread's PumpNetwork and the Simulation thread's Tick may
// run concurrently: what they share (the joined players and their commands) is guarded inside.
namespace augusta::server {

/// Everything a Host needs to construct SimulationWorld and start listening.
struct HostConfig {
  /// The rate, in Hz, at which the simulation ticks and every client predicts;
  /// told to each client when it joins. Fixed for the life of the server process.
  float tick_rate_hz = 0.0F;
  /// What the simulation runs on and what each client is told when it joins:
  /// the stamina rules of every player body, shared with PredictionWorld.
  parameters::Parameters parameters{};
  /// Lua game-policy script for SimulationWorld's Scripts/Behaviours phase.
  std::string script_path{};
  /// Local address to listen on (US-01).
  networking::Endpoint listen{};
};

/// The map's collision and where joining players spawn, as built by
/// augusta::map from the server pack by the caller: where content comes from
/// is the executable's business, not the config file's - so it travels
/// alongside HostConfig rather than inside it.
struct Map {
  std::vector<physics::CollisionMesh> collision;
  /// In the order joining players take them; empty spawns everyone at the origin.
  std::vector<math::Vec3> spawn_points;
};

/// The server's listening socket and its SimulationWorld, without threads or a clock.
class Host {
 public:
  /// Constructs SimulationWorld with map's collision (throws what its
  /// scripting engine throws if script_path fails to load, and
  /// std::runtime_error if a map mesh is rejected) and starts listening
  /// (throws std::runtime_error if the address can't be bound).
  Host(const HostConfig& config, Map map);
  ~Host();

  // Not copyable or movable: owns the listening socket.
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;
  Host(Host&&) = delete;
  Host& operator=(Host&&) = delete;

  /// Does one round of the Network I/O thread's work: connection events and received messages.
  void PumpNetwork();

  /// Runs one fixed tick of SimulationWorld on one command per player, sends each client its update, and returns the
  /// state.
  simulation::State Tick(float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HOST_H_
