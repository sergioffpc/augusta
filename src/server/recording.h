#ifndef AUGUSTA_SERVER_RECORDING_H_
#define AUGUSTA_SERVER_RECORDING_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/assets.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/math.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "frames.h"

/// \file
/// A match recording (ADR-0048): what server::Host handed its SimulationWorld
/// on each tick and what the tick resolved, so a replay (tools/replay/replay.h) can hand a
/// fresh World the same and check it resolves the same. RecordedSimulation is
/// the World as Host drives it, writing each tick to a Recorder when it has
/// one; ReadRecording reads a recording back. The records are the Networking
/// Protocol's, in its encoding (ADR-0038), converted in wire.h, so this header
/// names only the engine's types. Simulation thread only, as the World is; a
/// Recorder writes its stream on a thread of its own. A recording that cannot
/// be written is optional, and degrades while the Match goes on, or strict,
/// and is a terminal failure of the runtime that asks for it (ADR-0033).
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
inline constexpr std::size_t kMaxRecordSize = kRecordingFrames.max_payload;

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

/// What losing a recording's ticks costs the run (ADR-0048).
enum class RecordingMode : std::uint8_t {
  /// A debugging aid: the recording degrades, and the Match goes on as it
  /// would without one. augustad's default.
  kOptional,
  /// Evidence a replay or verification run needs whole: losing a tick is a
  /// terminal runtime failure (failure::Code::kStrictRecordingFailed).
  kStrict,
};

/// Where a recording is in its one-way life, as its logs and metrics name it.
enum class RecordingState : std::uint8_t {
  /// Every tick so far is queued or written.
  kEnabled,
  /// An optional recording lost a tick and writes nothing more; the run goes
  /// on without it.
  kDegraded,
  /// It writes nothing more: it was closed, or a strict recording lost a tick
  /// and the runtime is to stop on it.
  kStopped,
};

/// "optional" or "strict", as logs name mode.
[[nodiscard]] std::string_view RecordingModeName(RecordingMode mode);

/// "enabled", "degraded" or "stopped", as logs and metrics name state.
[[nodiscard]] std::string_view RecordingStateName(RecordingState state);

/// How a Recorder records, beyond its stream and header.
struct RecorderOptions {
  RecordingMode mode = RecordingMode::kOptional;
  /// Asked before each write (failure::Site::kRecordingWrite) and flush
  /// (kRecordingFlush), a trip failing the stream as the disk would; only a
  /// test gives one, and it must outlive the Recorder.
  failure::Faults* faults = nullptr;
  /// Called with each state the recording enters, from whichever thread
  /// enters it, kEnabled first, from the constructor; must outlive the
  /// Recorder. Empty calls nothing.
  std::function<void(RecordingState)> on_state;
  /// kRecordQueueCapacity but in tests.
  std::size_t capacity = kRecordQueueCapacity;
};

/// Writes a recording to a binary stream, which it does not own and which must
/// outlive it: each record as its 4-byte little-endian length and its payload,
/// the header first. Write, on the Simulation thread with the tick that made
/// the record (ADR-0048), only encodes it and queues it; a thread of the
/// Recorder's own writes and flushes each in turn, so the disk never holds up a
/// tick, and a recording outlives a server that stops abruptly up to the last
/// whole tick that thread wrote. Destroying the Recorder waits for every record
/// it queued to be written, then stops the recording.
///
/// A record longer than kMaxRecordSize, one that finds capacity records still
/// unwritten, or a write or flush the stream fails loses the recording's
/// ticks from there on: nothing more is written, so the file still reads back
/// up to its last whole tick, and its first loss is kept as Failure. An
/// optional recording then degrades, logged once at ERR as
/// event=recording_degraded; a strict one stops, logged at INFO as
/// event=recording_stopped, and the runtime that asks Failure stops on it,
/// whose boundary writes the one ERR line (ADR-0033). A record the protocol
/// cannot carry ends the recording too, but is the server's broken invariant
/// rather than the recording's loss: Write returns it, unlogged, for the
/// runtime to stop on.
class Recorder {
 public:
  /// Queues header for out first. Throws failure::ClassifiedFailure, a
  /// failure::Code::kInvariantViolated, if the protocol cannot carry header.
  Recorder(std::ostream& out, const RecordingHeader& header, RecorderOptions options = {});
  ~Recorder();
  Recorder(Recorder&&) noexcept;
  Recorder& operator=(Recorder&&) noexcept;
  Recorder(const Recorder&) = delete;
  Recorder& operator=(const Recorder&) = delete;

  /// Queues tick's record, without waiting for the stream, unless the
  /// recording has lost a tick already or broken an invariant. Fails, and
  /// queues nothing more, only when the protocol cannot carry the record:
  /// failure::Code::kInvariantViolated, with the tick in its context.
  [[nodiscard]] std::expected<void, failure::Failure> Write(const TickRecord& tick);

  /// Waits until every record queued so far is written, or dropped because
  /// the recording lost one, so State and Failure then account for every tick
  /// written so far. A stalled disk holds it up. Simulation thread.
  void WaitUntilWritten();

  /// A loss on the writer thread shows here once that thread gets to it,
  /// after the Write that queued the record has returned. Any thread.
  [[nodiscard]] RecordingState State() const;

  /// The first loss, nullopt while every tick is queued or written: a
  /// subsystem failure (kRecordingWriteFailed, kRecordingFlushFailed) of an
  /// optional recording, a runtime one (kStrictRecordingFailed) of a strict
  /// one, with the tick and the step (write, flush, queue_full or record_too_long) it was lost
  /// at as context. Any thread.
  [[nodiscard]] std::optional<failure::Failure> Failure() const;

 private:
  class Writer;
  std::unique_ptr<Writer> writer_;
  bool broken_ = false;
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

  /// As Recorder::WaitUntilWritten, if it records.
  void WaitUntilRecorded();

  /// The recording's first loss (Recorder::Failure), nullopt while it has
  /// lost no tick or when nothing is recorded.
  [[nodiscard]] std::optional<failure::Failure> RecordingFailure() const;

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
