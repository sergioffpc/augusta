#ifndef AUGUSTA_SERVER_RECORDING_H_
#define AUGUSTA_SERVER_RECORDING_H_

#include <cstdint>
#include <expected>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"

/// \file
/// A match recording (ADR-0050): what server::Host handed its SimulationWorld
/// on each tick and what the tick resolved, so a replay (replay.h) can hand a
/// fresh World the same and check it resolves the same. RecordedSimulation is
/// the World as Host drives it, writing each tick to a Recorder when it has
/// one; ReadRecording reads a recording back. The records are the Networking
/// Protocol's, in its encoding (ADR-0038), converted in wire.h, so this header
/// names only the engine's types. Simulation thread only, as the World is.
namespace augusta::server {

/// What a recording was made on.
struct RecordingHeader {
  /// The recording engine's version (augusta::EngineVersion): a replay on
  /// another is not held to the same bits (replay.h).
  std::string engine_version;
  /// The server pack the World's content was loaded from: a replay loads the same.
  assets::PackHash server_pack{};
  std::uint8_t tick_rate_hz = 0;

  bool operator==(const RecordingHeader&) const = default;
};

/// One player of a Match start as SimulationWorld was handed it, its Character
/// by path: a replay takes the hitboxes from the content it loads.
struct RecordedEntrant {
  simulation::EntityId entity{};
  simulation::PlayerIdentity identity{};
};

/// What SimulationWorld was handed before and on one tick, in the order
/// RecordedSimulation hands it: the Match in it ended, bodies removed, a Match
/// started, then the tick run. Every command is on the protocol's grids, as
/// every command the server takes in off the wire is.
struct TickInput {
  std::vector<simulation::EntityId> removed;
  /// Empty when no Match started: a Match always has a player.
  std::vector<RecordedEntrant> match_start;
  std::vector<simulation::PlayerCommand> commands;
  /// In seconds.
  float delta_time = 0.0F;
  bool match_ended = false;
};

/// What one tick resolved that its players are told of: a replay that resolves
/// the same has the same outcome (replay.h). Every number is on the protocol's
/// grids or travels as its bits, so a recorded one reads back exactly.
struct TickOutcome {
  /// The tick, as the World numbers it, from 1.
  tick::Tick tick = 0;
  /// Where each player of the tick's Match start spawned, on the position grid.
  std::vector<math::Vec3> spawns;
  std::vector<simulation::EntityState> bodies;
  std::vector<simulation::Shot> shots;
  std::vector<simulation::Hit> hits;
  std::vector<simulation::Death> deaths;
  /// Game policy's Match end of the tick, if it took one.
  std::optional<simulation::MatchEnd> match_end;
};

/// The outcome of a tick that resolved result, whose Match start, if it had
/// one, spawned its players at spawns.
[[nodiscard]] TickOutcome OutcomeOf(const std::vector<math::Vec3>& spawns, const simulation::TickResult& result);

/// One tick of a recording.
struct TickRecord {
  TickInput input;
  TickOutcome outcome;
};

/// A whole recording, its ticks in the order they ran, from the World's first.
struct Recording {
  RecordingHeader header;
  std::vector<TickRecord> ticks;
  /// Whether the file ended partway through a tick's record, which was
  /// dropped: the server stopped while writing it.
  bool torn = false;
};

/// Why a recording could not be read.
enum class RecordingError : std::uint8_t {
  /// The stream could not be read.
  kUnreadable,
  /// It does not start with a header record.
  kNoHeader,
  /// A record does not decode, or a second header follows the first.
  kMalformed,
};

/// What to tell whoever runs the process about error.
[[nodiscard]] std::string_view DescribeRecordingError(RecordingError error);

/// Writes a recording to a binary stream, which it does not own: each record
/// as its 4-byte little-endian length and its payload, the header first, and
/// flushed after each tick, so a recording outlives a server that stops
/// abruptly up to its last whole tick.
class Recorder {
 public:
  /// Writes header to out.
  Recorder(std::ostream& out, const RecordingHeader& header);

  void Write(const TickRecord& tick);

 private:
  std::ostream* out_;
};

/// Reads a recording a Recorder wrote. A last record cut short is dropped and
/// reported in Recording::torn.
[[nodiscard]] std::expected<Recording, RecordingError> ReadRecording(std::istream& in);

/// SimulationWorld as server::Host drives it: the World's own calls, each
/// noted for the tick it precedes and, with a Recorder, written with what the
/// tick resolves when it runs.
class RecordedSimulation {
 public:
  /// world, recording to recorder if it has one.
  RecordedSimulation(simulation::World world, std::optional<Recorder> recorder);

  /// As simulation::World::EndMatch.
  void EndMatch();
  /// As simulation::World::RemovePlayer.
  void RemovePlayer(simulation::EntityId entity);
  /// As simulation::World::StartMatch; at most once between two Ticks.
  std::vector<math::Vec3> StartMatch(const std::vector<simulation::MatchPlayer>& players,
                                     const std::vector<math::Vec3>& spawn_points);
  /// As simulation::World::Tick, then writes the tick's record.
  simulation::TickResult Tick(const std::vector<simulation::PlayerCommand>& commands, float delta_time);

 private:
  simulation::World world_;
  std::optional<Recorder> recorder_;
  // What the coming tick was handed so far, and where its Match start spawned its players.
  TickInput pending_;
  std::vector<math::Vec3> pending_spawns_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_RECORDING_H_
