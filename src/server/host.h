#ifndef AUGUSTA_SERVER_HOST_H_
#define AUGUSTA_SERVER_HOST_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"
#include "match.h"
#include "parameters_loader.h"

// augusta::server::Host is the server's network boundary and the
// authoritative SimulationWorld (ADR-0023) without the threads and the clock:
// ServerRuntime (src/server) runs PumpNetwork on the Network I/O thread and
// Tick on the Simulation thread at a fixed rate (ADR-0005), while a test calls
// both by hand, so a match can be driven tick by tick with no sleeping.
//
// Admitted players wait in the Lobby, and a match starts on the tick the Lobby
// is full and everyone is Ready (ADR-0043): only then are bodies simulated and
// Authoritative States sent, and only to the players in the match.
//
// The Network I/O thread's PumpNetwork and the Simulation thread's Tick may
// run concurrently: what they share (the Lobby, the match and the players'
// commands) is guarded inside.
namespace augusta::server {

/// Everything a Host needs to construct SimulationWorld and start listening.
struct HostConfig {
  /// The rate, in Hz, at which the simulation ticks and every client predicts;
  /// told to each client when it joins. Fixed for the life of the server process.
  std::uint8_t tick_rate_hz = 0;
  /// What the simulation runs on and what each client is told when it joins:
  /// the stamina rules of every player body, shared with PredictionWorld, and
  /// the Player count a match starts with.
  parameters::Parameters parameters{};
  /// Lua game-policy script for SimulationWorld's Scripts/Behaviours phase.
  std::string script_path;
  /// Local address to listen on (US-01).
  networking::Endpoint listen{};
};

/// The map's collision and where joining players spawn, as built by
/// augusta::map from the server pack by the caller: where content comes from
/// is the executable's business, not the config file's - so it travels
/// alongside HostConfig rather than inside it.
struct Map {
  std::vector<physics::CollisionMesh> collision;
  /// In the order players take them at each match start, continuing across
  /// matches; empty spawns everyone at the origin.
  std::vector<math::Vec3> spawn_points;
  /// The scenario's characters, by path: the only ones a player may join as
  /// (ADR-0042). Empty admits no one.
  std::vector<std::string> characters;
  /// The hash of the client pack cooked with the server's: the only one a
  /// player may join with.
  assets::PackHash client_pack{};
};

/// entity as SimulationWorld names the same body.
[[nodiscard]] simulation::EntityId ToSimulation(EntityId entity);

/// A SimulationWorld body's entity as Match named it; the inverse of ToSimulation.
[[nodiscard]] EntityId FromSimulation(simulation::EntityId entity);

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

  /// Runs one fixed tick of SimulationWorld on one command per player in the
  /// match, sends each of them its update, and returns the state. Starts a
  /// match first if the Lobby is full and Ready and the pause after the last
  /// one (server::kMatchPause, counted in these ticks) has passed.
  simulation::State Tick(float delta_time);

  /// Ends the match in progress, if any: its players are sent Match end and are
  /// back in the Lobby, and their bodies leave the simulation on the next Tick.
  /// Game policy's way to end a match (and a test's). From the Simulation
  /// thread, between Ticks.
  void EndMatch();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HOST_H_
