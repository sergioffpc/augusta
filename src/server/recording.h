#ifndef AUGUSTA_SERVER_RECORDING_H_
#define AUGUSTA_SERVER_RECORDING_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/assets.h"
#include "augusta/failure.h"
#include "augusta/math.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"

/// \file
/// A match recording (ADR-0048): what server::Host handed its SimulationWorld
/// on each tick and what the tick resolved, so a replay (tools/replay/replay.h) can hand a
/// fresh World the same and check it resolves the same. RecordedSimulation is
/// the World as Host drives it, writing each tick to a Recorder when it has
/// one; ReadRecording reads a recording back. The records are the Networking
/// Protocol's, in its encoding (ADR-0038), converted in wire.h, so this header
/// names only the engine's types. Simulation thread only, as the World is; a
/// Recorder writes its stream on a thread of its own.
namespace augusta::server {

/// What a recording was made on.
struct RecordingHeader {
  /// The recording engine's version (augusta::EngineVersion): a replay on
  /// another is not held to exactly the same outcome (tools/replay/replay.h).
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
/// the same has the same outcome (tools/replay/replay.h). Every number is on the protocol's
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

/// The longest record a recording holds, in bytes: many times the largest a
/// tick of the most players makes, so a length past it is a corrupted one, and
/// is refused before anything is allocated for it.
inline constexpr std::size_t kMaxRecordSize = std::size_t{64} * 1024;

enum class RecordingError : std::uint8_t {
  /// The stream could not be read, or was never opened.
  kUnreadable,
  /// It does not start with a header record.
  kNoHeader,
  /// A record does not decode, or a second header follows the first.
  kMalformed,
};

/// What to tell whoever runs the process about error.
[[nodiscard]] std::string_view DescribeRecordingError(RecordingError error);

/// How many records a Recorder holds that its writer has not yet written:
/// about 4 seconds of ticks at 60 Hz, past which the disk is not keeping up and
/// the recording stops. At most kMaxRecordSize each, so 16 MiB at worst.
inline constexpr std::size_t kRecordQueueCapacity = 256;

/// Writes a recording to a binary stream, which it does not own and which must
/// outlive it: each record as its 4-byte little-endian length and its payload,
/// the header first. Write, on the Simulation thread with the tick that made
/// the record (ADR-0048), only encodes it and queues it; a thread of the
/// Recorder's own writes and flushes each in turn, so the disk never holds up a
/// tick, and a recording outlives a server that stops abruptly up to the last
/// whole tick that thread wrote. Destroying the Recorder waits for every record
/// it queued to be written, so a stalled disk holds up shutdown, never a tick.
///
/// A record longer than kMaxRecordSize, one that finds capacity records still
/// unwritten, or a write the stream fails stops the recording: it is logged
/// once, as event=recording_stopped, and nothing more is written, so the file
/// still reads back up to its last whole tick. A record the protocol cannot
/// carry stops it too, but is the server's broken invariant rather than the
/// recording's failure: Write returns it, unlogged, for the runtime to stop on
/// (ADR-0033).
class Recorder {
 public:
  /// Queues header for out first, and holds at most capacity records not yet
  /// written; capacity is kRecordQueueCapacity but in tests. Throws
  /// std::runtime_error if the protocol cannot carry header.
  Recorder(std::ostream& out, const RecordingHeader& header, std::size_t capacity = kRecordQueueCapacity);
  ~Recorder();
  Recorder(Recorder&&) noexcept;
  Recorder& operator=(Recorder&&) noexcept;
  Recorder(const Recorder&) = delete;
  Recorder& operator=(const Recorder&) = delete;

  /// Queues tick's record, without waiting for the stream, unless the
  /// recording has stopped. Fails, and stops the recording, only when the
  /// protocol cannot carry the record: failure::Code::kInvariantViolated, with
  /// the tick in its context.
  [[nodiscard]] std::expected<void, failure::Failure> Write(const TickRecord& tick);

  /// Whether the recording has stopped, its file missing every tick since. A
  /// write the stream fails stops it once the writer thread gets to it.
  [[nodiscard]] bool Stopped() const;

 private:
  // Logs that the recording stopped on tick, and why, and queues nothing more.
  void Stop(tick::Tick tick, std::string_view reason);

  class Writer;
  std::unique_ptr<Writer> writer_;
  bool stopped_ = false;
};

/// Reads a recording a Recorder wrote. A last record cut short is dropped and
/// reported in Recording::torn; a stream that fails, wherever it does, is
/// kUnreadable, never a whole or torn recording.
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

  /// The broken invariant the first record the protocol could not carry was
  /// (Recorder::Write), or nullopt: once set, the runtime must stop.
  [[nodiscard]] const std::optional<failure::Failure>& Failure() const;

 private:
  simulation::World world_;
  std::optional<Recorder> recorder_;
  std::optional<failure::Failure> failure_;
  // What the coming tick was handed so far, and where its Match start spawned its players.
  TickInput pending_;
  std::vector<math::Vec3> pending_spawns_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_RECORDING_H_
