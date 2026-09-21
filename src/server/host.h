#ifndef AUGUSTA_SERVER_HOST_H_
#define AUGUSTA_SERVER_HOST_H_

#include <cstdint>
#include <expected>
#include <filesystem>
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
  /// What the simulation runs on and what each client is told when it joins:
  /// the stamina rules of every player body, shared with PredictionWorld.
  parameters::Parameters parameters{};
  /// The Parameters script Host::Reload reads again (ADR-0039); the Parameters
  /// above are what it held when the server started.
  std::filesystem::path parameters_path{};
  /// Lua game-policy script for SimulationWorld's Scripts/Behaviours phase.
  std::string script_path{};
  /// Local address to listen on (US-01).
  networking::Endpoint listen{};
  /// The map's collision, as built by augusta::map from the server pack.
  std::vector<physics::CollisionMesh> collision{};
  /// Where joining players spawn, in the order they take them, from the same
  /// pack; empty spawns everyone at the origin.
  std::vector<math::Vec3> spawn_points{};
};

/// Why a reload of the Parameters script was refused.
enum class ReloadRefusal {
  /// The script could not be read or did not load; see ReloadError::load.
  kLoadFailed,
  /// The script gives a tick rate other than the running one, which is fixed
  /// for the life of the server process: restart the server to change it.
  kTickRateChanged,
};

/// A reload that changed nothing, and why.
struct ReloadError {
  ReloadRefusal reason{};
  /// Why the script did not load; only for kLoadFailed.
  parameters::LoadError load{};
};

/// A message for error fit to log, so no caller words it on its own.
std::string DescribeReloadError(const ReloadError& error);

/// The server's listening socket and its SimulationWorld, without threads or a clock.
class Host {
 public:
  /// Constructs SimulationWorld with the map's collision (throws what its
  /// scripting engine throws if script_path fails to load, and
  /// std::runtime_error if a map mesh is rejected) and starts listening
  /// (throws std::runtime_error if the address can't be bound).
  explicit Host(const HostConfig& config);
  ~Host();

  // Not copyable or movable: owns the listening socket.
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;
  Host(Host&&) = delete;
  Host& operator=(Host&&) = delete;

  /// Does one round of the Network I/O thread's work: connection events and received messages.
  void PumpNetwork();

  /// Reads the Parameters script again and, if it loads and keeps the tick rate,
  /// makes it the next generation: the simulation runs on it from the start of
  /// the next Tick, never partway through one, and the generation is numbered
  /// from the last accepted one. Returns that number. A script that fails to
  /// load, or changes the tick rate, is refused as a whole: nothing changes,
  /// the reason is logged and no number is used. Safe to call from any thread.
  std::expected<std::uint32_t, ReloadError> Reload();

  /// The generation of the Parameters the simulation runs on: 1 at startup, and
  /// the reloaded one once a Tick has begun since. Safe to call from any thread.
  [[nodiscard]] std::uint32_t Generation() const;

  /// Runs one fixed tick of SimulationWorld on one command per player, sends each client its update, and returns the
  /// state.
  simulation::State Tick(float delta_time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HOST_H_
