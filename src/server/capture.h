#ifndef AUGUSTA_SERVER_CAPTURE_H_
#define AUGUSTA_SERVER_CAPTURE_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/math.h"
#include "augusta/tick.h"
#include "capture_retention.h"
#include "match.h"

/// \file
/// A Match capture (ADR-0050): one file per Match, from its Match start to its
/// Match end, of what its players did (Join, Commands, Leave) and of the
/// markers the server decided (Death, Match end), each at its offset in ticks
/// from the Match's first tick. server::Host hands a Capturer each event on the
/// Simulation thread as the tick makes it; the Capturer encodes it there and a
/// thread of its own opens, writes and flushes the files, so a disk that
/// stalls never holds up a tick (NFR-01). ReadCapture reads a file back. The
/// records are the Networking Protocol's, in its encoding, converted in
/// wire.h, so this header names only the engine's types. A capture that cannot
/// be written stops, logged once, and the Match goes on without it: a capture
/// is a debugging aid, never a runtime failure.
namespace augusta::server {

/// What a captured Match ran on and when it started.
struct CaptureHeader {
  /// The capturing engine's version (augusta::EngineVersion).
  std::string engine_version;
  assets::PackHash server_pack{};
  assets::PackHash client_pack{};
  std::uint8_t tick_rate_hz = 0;
  /// When the Match started, in UTC.
  std::chrono::sys_time<std::chrono::milliseconds> started;

  bool operator==(const CaptureHeader&) const = default;
};

/// A player's number in a capture: from 1, in its Match start's order, as
/// `augustac --reenact --player` names it.
using CapturedPlayer = std::uint8_t;

/// A player of the Match start, where it spawned on the position grid.
struct CapturedJoin {
  CapturedPlayer player = 0;
  SessionId session{};
  /// Its Character, by its name in the scenario's manifest (ADR-0042).
  std::string character;
  math::Vec3 spawn{};

  bool operator==(const CapturedJoin&) const = default;
};

/// A Command its player sent, as the command queue handed it to SimulationWorld.
struct CapturedCommand {
  CapturedPlayer player = 0;
  /// Its Seen time's tick, as an offset from the Match's first tick: negative
  /// for a State sent before the Match started. command.seen_tick is 0.
  std::int32_t seen_offset = 0;
  command::Command command{};
};

/// A player whose connection ended mid-Match.
struct CapturedLeave {
  CapturedPlayer player = 0;

  bool operator==(const CapturedLeave&) const = default;
};

struct CapturedDeath {
  CapturedPlayer victim = 0;
  CapturedPlayer killer = 0;

  bool operator==(const CapturedDeath&) const = default;
};

struct CapturedMatchEnd {
  /// nullopt for a Draw.
  std::optional<CapturedPlayer> winner;

  bool operator==(const CapturedMatchEnd&) const = default;
};

using CapturedEvent = std::variant<CapturedJoin, CapturedCommand, CapturedLeave, CapturedDeath, CapturedMatchEnd>;

/// One event of a capture, at its offset in ticks from the Match's first tick.
struct CaptureRecord {
  std::uint32_t offset = 0;
  CapturedEvent event;
};

/// A whole capture, its records in the order they were written.
struct Capture {
  CaptureHeader header;
  std::vector<CaptureRecord> records;
  /// Whether the file ended partway through a record, which was dropped: the
  /// server stopped while writing it.
  bool torn = false;
};

enum class CaptureError : std::uint8_t {
  /// The stream could not be read, or was never opened.
  kUnreadable,
  /// It does not start with a capture's magic.
  kNotACapture,
  /// Its header is missing, or of a format version this engine does not read.
  kUnsupported,
  /// A record does not decode, comes out of order, or names a player no Join did.
  kMalformed,
};

/// What to tell whoever runs the process about error.
[[nodiscard]] std::string_view DescribeCaptureError(CaptureError error);

/// Reads a capture a Capturer wrote. A last record cut short is dropped and
/// reported in Capture::torn; a stream that fails, wherever it does, is
/// kUnreadable, never a whole or torn capture.
[[nodiscard]] std::expected<Capture, CaptureError> ReadCapture(std::istream& in);

/// The name of the file of the Match that started at started, the
/// match_number-th of its server's run, from 1: its start in UTC, then its
/// number, so the names of one server's captures sort by when they started.
[[nodiscard]] std::string CaptureFileName(std::chrono::sys_time<std::chrono::milliseconds> started,
                                          std::uint64_t match_number);

/// Why a Match's capture stopped before its Match end, as logs name it.
enum class CaptureStop : std::uint8_t {
  /// A record is longer than a capture's frame holds (kCaptureFrames).
  kRecordTooLong,
  /// A record found kCaptureQueueCapacity records still unwritten: the disk is not keeping up.
  kQueueFull,
  /// The file could not be created, written or flushed.
  kWriteFailed,
  /// The record would take the directory past CaptureRetention::max_bytes with
  /// no completed capture left to delete: the Match alone fills it.
  kRetentionBudget,
};

/// "record_too_long", "queue_full", "write_failed" or "retention_budget".
[[nodiscard]] std::string_view CaptureStopName(CaptureStop stop);

/// Whether a server captures, as augustad_capture_state names it.
enum class CaptureState : std::uint8_t {
  /// Captures are not configured: the server has no Capturer.
  kOff,
  /// No Match is open.
  kIdle,
  /// The Match open is being captured.
  kCapturing,
  /// The Match open's capture stopped early (CaptureStop), until its Match end.
  kStopped,
};

/// What a capture directory holds of captures: the files CaptureDirectory
/// counts as captures, whoever wrote them.
struct CaptureDirectoryUsage {
  std::uint64_t files = 0;
  std::uint64_t bytes = 0;

  bool operator==(const CaptureDirectoryUsage&) const = default;
};

/// Told what a Capturer does, for the server's metrics (HostMetrics) to count:
/// a Capturer knows nothing of how it is counted. Each call comes from the
/// Simulation thread or the Capturer's writer, some with the writer's lock
/// held, so none may block (NFR-01). Each does nothing unless overridden.
class CaptureObserver {
 public:
  CaptureObserver() = default;
  virtual ~CaptureObserver() = default;
  CaptureObserver(const CaptureObserver&) = delete;
  CaptureObserver& operator=(const CaptureObserver&) = delete;
  CaptureObserver(CaptureObserver&&) = delete;
  CaptureObserver& operator=(CaptureObserver&&) = delete;

  /// The server's captures entered state: kIdle as the Capturer is made, then
  /// as each Match opens, stops or ends.
  virtual void OnState(CaptureState /*state*/) {}
  /// The writer created a Match's file.
  virtual void OnStarted() {}
  /// The writer closed a Match's file with every record of it written.
  virtual void OnCompleted() {}
  /// A Match's capture stopped early, told once per capture.
  virtual void OnStopped(CaptureStop /*stop*/) {}
  /// The writer wrote bytes more to a file: its magic or a record's frame.
  virtual void OnWritten(std::size_t /*bytes*/) {}
  /// records are queued for the writer and not yet written.
  virtual void OnQueued(std::size_t /*records*/) {}
  /// What the capture directory holds, the capture being written included, as
  /// the writer scans it as each Match's file is about to be created, then as
  /// it writes and as retention deletes (CaptureDirectory).
  virtual void OnDirectory(CaptureDirectoryUsage /*usage*/) {}
  /// Retention deleted a completed capture (CaptureRetention).
  virtual void OnRetentionDeleted() {}
  /// Retention could not delete a completed capture.
  virtual void OnRetentionDeleteFailed() {}
};

/// How many records a Capturer holds that its writer has not yet written:
/// about 4 seconds of a full Match's Commands at 60 Hz, past which the disk is
/// not keeping up and the capture stops.
inline constexpr std::size_t kCaptureQueueCapacity = 4096;

/// One player of a Match start, as Host hands it to a Capturer.
struct CaptureEntrant {
  SessionId session{};
  /// The body its Commands move, which the Simulation's events name it by.
  EntityId entity{};
  std::string character;
  /// Where SimulationWorld spawned it.
  math::Vec3 spawn{};
};

/// How a Capturer captures, beyond its directory and header.
struct CaptureOptions {
  /// Asked before each write (failure::Site::kCaptureWrite), a trip failing
  /// the file as the disk would; only a test gives one, and it must outlive
  /// the Capturer.
  failure::Faults* faults = nullptr;
  /// kCaptureQueueCapacity but in tests.
  std::size_t capacity = kCaptureQueueCapacity;
  /// What the directory is kept within; off by default, deleting nothing.
  CaptureRetention retention{};
  /// Told what the Capturer does, if given; it must outlive the Capturer.
  CaptureObserver* observer = nullptr;
};

/// Captures every Match a server runs into a directory of its own, one file
/// each (CaptureFileName), each file the capture's magic then its records,
/// framed as frames.h writes kCaptureFrames, the header first, the directory
/// kept within CaptureOptions::retention (capture_retention.h). Every call but the
/// destructor is the Simulation thread's, in the order the tick makes its
/// events, and only encodes and queues; the Capturer's writer thread creates,
/// writes, flushes and closes the files. A Match's capture stops at a record
/// too long, one that finds the queue full, a failed write, or one past the
/// retention budget, logged once at WARN as event=capture_stopped: its file
/// keeps every whole record before it,
/// and the next Match is captured afresh. Destroying the Capturer waits for
/// every record queued to be written. Its CaptureOptions::observer, if any, is
/// told each of these as it happens, on the thread it happens on.
class Capturer {
 public:
  /// Captures into directory, which must exist, each file under header with
  /// its Match's start time.
  Capturer(std::filesystem::path directory, CaptureHeader header, CaptureOptions options = {});
  ~Capturer();
  Capturer(const Capturer&) = delete;
  Capturer& operator=(const Capturer&) = delete;
  Capturer(Capturer&&) = delete;
  Capturer& operator=(Capturer&&) = delete;

  /// Opens a Match's capture, ending one still open: its players, in their
  /// Match start's order, first_tick its first tick, started when it started.
  void StartMatch(const std::vector<CaptureEntrant>& players, tick::Tick first_tick,
                  std::chrono::system_clock::time_point started);

  /// The Command the player of entity sent, handed to SimulationWorld on tick.
  void Command(tick::Tick tick, EntityId entity, const command::Command& command);

  /// The player of entity left: its body was taken out before tick.
  void Leave(tick::Tick tick, EntityId entity);

  /// The player of victim died on tick, killed by the player of killer.
  void Death(tick::Tick tick, EntityId victim, EntityId killer);

  /// The Match ended after last_tick, its last, won by winner or drawn; its
  /// file is closed once written.
  void EndMatch(tick::Tick last_tick, std::optional<SessionId> winner);

  /// Waits until every record queued so far is written, or dropped because
  /// its capture stopped. A stalled disk holds it up. A test's only.
  void WaitUntilWritten();

  /// The broken invariant the first record the protocol could not carry was
  /// (failure::Code::kInvariantViolated), or nullopt: once set, the runtime
  /// must stop (ADR-0033). Nothing of that record is written.
  [[nodiscard]] const std::optional<failure::Failure>& Failure() const;

 private:
  class Writer;

  // The player number of entity in the Match open, 0 if it has none.
  [[nodiscard]] CapturedPlayer PlayerOf(EntityId entity) const;
  // Offset of tick from the Match's first.
  [[nodiscard]] std::uint32_t OffsetOf(tick::Tick tick) const;
  // encoded, a record EncodeToCapture made, if a capture's frame holds it.
  // Otherwise nullopt: a broken invariant is kept as Failure, and a record too
  // long stops the Match's capture.
  std::optional<std::vector<std::byte>> Admit(std::expected<std::vector<std::byte>, failure::Failure> encoded);
  void Queue(const CaptureRecord& record);

  const std::filesystem::path directory_;
  const CaptureHeader header_;
  std::unique_ptr<Writer> writer_;
  // The Match open, if any: how many Matches started in the run, its first
  // tick, and the number of each of its players' bodies.
  std::uint64_t matches_ = 0;
  bool open_ = false;
  tick::Tick first_tick_ = 0;
  std::unordered_map<EntityId, CapturedPlayer> players_;
  std::unordered_map<SessionId, CapturedPlayer> sessions_;
  std::optional<failure::Failure> failure_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_CAPTURE_H_
