#include "augusta/capturer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/capture.h"
#include "augusta/capture_records.h"
#include "augusta/capture_retention.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/grid.h"
#include "augusta/logging.h"
#include "augusta/match.h"
#include "augusta/tick.h"
#include "augusta/utf8.h"
#include "augusta/uuid.h"

namespace augusta::host {

namespace {

// What directory's retention reports, told to observer: the directory's
// files and bytes, and each deletion and failed one.
CaptureRetentionObserver RetentionReports(CaptureObserver& observer) {
  return CaptureRetentionObserver{
      .on_deleted = [&observer] { observer.OnRetentionDeleted(); },
      .on_delete_failed = [&observer] { observer.OnRetentionDeleteFailed(); },
      .on_directory = [&observer](std::size_t files,
                                  std::uintmax_t bytes) { observer.OnDirectory({.files = files, .bytes = bytes}); },
  };
}

// Why a capture lost at step stopped, as its capture_stopped line names it.
CaptureStop StopAt(CaptureStep step) {
  switch (step) {
    case CaptureStep::kQueueFull:
      return CaptureStop::kQueueFull;
    case CaptureStep::kRecordTooLong:
      return CaptureStop::kRecordTooLong;
    case CaptureStep::kRetentionBudget:
      return CaptureStop::kRetentionBudget;
    case CaptureStep::kCreate:
    case CaptureStep::kWrite:
    case CaptureStep::kFlush:
      return CaptureStop::kWriteFailed;
  }
  return CaptureStop::kWriteFailed;
}

// What an optional capture's loss at step is: a slow disk, an oversized
// record and a full budget are named apart from a disk that refused the file.
failure::Code OptionalLossCode(CaptureStep step) {
  switch (step) {
    case CaptureStep::kFlush:
      return failure::Code::kCaptureFlushFailed;
    case CaptureStep::kQueueFull:
      return failure::Code::kCaptureQueueFull;
    case CaptureStep::kRecordTooLong:
      return failure::Code::kCaptureRecordTooLong;
    case CaptureStep::kRetentionBudget:
      return failure::Code::kCaptureRetentionBudget;
    case CaptureStep::kCreate:
    case CaptureStep::kWrite:
      return failure::Code::kCaptureWriteFailed;
  }
  return failure::Code::kCaptureWriteFailed;
}

// How many ticks to is after from: negative when it is before.
std::int64_t Offset(tick::Tick from, tick::Tick to) {
  return static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
}

}  // namespace

std::string_view CaptureStopName(CaptureStop stop) {
  switch (stop) {
    case CaptureStop::kRecordTooLong:
      return "record_too_long";
    case CaptureStop::kQueueFull:
      return "queue_full";
    case CaptureStop::kWriteFailed:
      return "write_failed";
    case CaptureStop::kRetentionBudget:
      return "retention_budget";
  }
  return "unknown";
}

std::string_view CaptureStepName(CaptureStep step) {
  switch (step) {
    case CaptureStep::kCreate:
      return "create";
    case CaptureStep::kWrite:
      return "write";
    case CaptureStep::kFlush:
      return "flush";
    case CaptureStep::kQueueFull:
      return "queue_full";
    case CaptureStep::kRecordTooLong:
      return "record_too_long";
    case CaptureStep::kRetentionBudget:
      return "retention_budget";
  }
  return "unknown";
}

failure::Failure LostCapture(CaptureMode mode, const uuid::Uuid& match, CaptureStep step, std::string detail) {
  return {.code = mode == CaptureMode::kStrict ? failure::Code::kStrictCaptureFailed : OptionalLossCode(step),
          .context = {{.key = "match", .value = match.ToString()},
                      {.key = "step", .value = std::string(CaptureStepName(step))}},
          .detail = std::move(detail)};
}

std::string_view CaptureModeName(CaptureMode mode) {
  switch (mode) {
    case CaptureMode::kOptional:
      return "optional";
    case CaptureMode::kStrict:
      return "strict";
  }
  return "unknown";
}

std::string_view CaptureHealthName(CaptureHealth health) {
  switch (health) {
    case CaptureHealth::kEnabled:
      return "enabled";
    case CaptureHealth::kDegraded:
      return "degraded";
    case CaptureHealth::kStopped:
      return "stopped";
  }
  return "unknown";
}

// The thread a Capturer writes its files on and what is queued for it, oldest
// first: each Match's file opened, its records, and its file closed, each
// item naming its Match by its Match ID. The Simulation thread only
// ever waits on the lock, which the writer holds to take an item and never
// across a write. It keeps the run's first loss and the health it leaves the
// captures in, and tells its observer what it does, the state of captures
// under the lock so that a stop the writer finds is never told after the
// Simulation thread has moved to another Match.
class Capturer::Writer {
 public:
  Writer(std::filesystem::path directory, CaptureOptions options)
      : mode_(options.mode),
        faults_(options.faults),
        capacity_(options.capacity),
        observer_(options.observer != nullptr ? options.observer : &unobserved_),
        directory_(std::move(directory), std::move(options.retention), RetentionReports(*observer_), options.faults) {
    observer_->OnState(CaptureState::kIdle);
    Enter(CaptureHealth::kEnabled);
    thread_ = std::thread([this] { Run(); });
  }

  // Writes what is still queued before it goes, then stops the captures.
  ~Writer() {
    {
      const std::scoped_lock lock(mutex_);
      closing_ = true;
    }
    ready_.notify_one();
    thread_.join();
    const std::scoped_lock lock(mutex_);
    StopCapturesLocked();
  }

  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;
  Writer(Writer&&) = delete;
  Writer& operator=(Writer&&) = delete;

  // match is the Match open, captured until its Close or a stop.
  void Begin(const uuid::Uuid& match) {
    const std::scoped_lock lock(mutex_);
    open_match_ = match;
    observer_->OnState(CaptureState::kCapturing);
  }

  void Open(const uuid::Uuid& match, std::filesystem::path path, capture::Encoded header) {
    Enqueue({.kind = Kind::kOpen, .match = match, .encoded = std::move(header), .path = std::move(path)});
  }

  void Close(const uuid::Uuid& match) {
    {
      const std::scoped_lock lock(mutex_);
      if (open_match_ == match) {
        open_match_ = uuid::kNil;
        observer_->OnState(CaptureState::kIdle);
      }
      queue_.push_back({.kind = Kind::kClose, .match = match, .encoded = std::nullopt, .path = {}});
    }
    ready_.notify_one();
  }

  // Queues match's record, or stops its capture there when capacity records
  // are still unwritten. Once its capture has stopped, drops every one.
  void Push(const uuid::Uuid& match, capture::Encoded record) {
    {
      const std::scoped_lock lock(mutex_);
      if (stopped_.contains(match) || health_ == CaptureHealth::kStopped) {
        return;
      }
      // Dropping this record and going on would leave a capture missing one
      // in its middle; stopping here keeps every one before it.
      if (records_queued_ >= capacity_) {
        StopLocked(match, CaptureStep::kQueueFull, {});
        return;
      }
      ++records_queued_;
      observer_->OnQueued(records_queued_);
      queue_.push_back({.kind = Kind::kRecord, .match = match, .encoded = std::move(record), .path = {}});
    }
    ready_.notify_one();
  }

  void Stop(const uuid::Uuid& match, CaptureStep step) {
    const std::scoped_lock lock(mutex_);
    StopLocked(match, step, {});
  }

  void WaitUntilWritten() {
    std::unique_lock lock(mutex_);
    written_.wait(lock, [this] { return queue_.empty() && !writing_; });
  }

  // Waits until what is queued is written, then stops the captures: nothing
  // queued after is written.
  void Finish() {
    std::unique_lock lock(mutex_);
    written_.wait(lock, [this] { return queue_.empty() && !writing_; });
    StopCapturesLocked();
  }

  [[nodiscard]] CaptureHealth Health() const { return health_; }

  [[nodiscard]] std::optional<failure::Failure> Loss() const {
    const std::scoped_lock lock(mutex_);
    return loss_;
  }

 private:
  enum class Kind : std::uint8_t { kOpen, kRecord, kClose };

  // Where the writer lost a Match's file, and what the disk said of it.
  struct Failed {
    CaptureStep step;
    std::string detail;
  };

  struct Item {
    Kind kind;
    uuid::Uuid match;
    // kOpen: the magic with the header; kRecord: the record.
    std::optional<capture::Encoded> encoded;
    std::filesystem::path path;
  };

  // Queues an item that opens a file: never refused, since it is one per Match.
  void Enqueue(Item item) {
    {
      const std::scoped_lock lock(mutex_);
      queue_.push_back(std::move(item));
    }
    ready_.notify_one();
  }

  void Run() {
    std::unique_lock lock(mutex_);
    while (true) {
      ready_.wait(lock, [this] { return closing_ || !queue_.empty(); });
      if (queue_.empty()) {
        return;
      }
      Item item = std::move(queue_.front());
      queue_.pop_front();
      if (item.kind == Kind::kRecord) {
        --records_queued_;
        observer_->OnQueued(records_queued_);
      }
      writing_ = true;
      lock.unlock();
      std::optional<Failed> loss = HandleGuarded(item);
      lock.lock();
      writing_ = false;
      if (loss.has_value()) {
        // Whatever of the record reached the file reads back as a torn last
        // record; writing on would put whole records after it no reader reaches.
        std::erase_if(queue_,
                      [&](const Item& queued) { return queued.kind == Kind::kRecord && queued.match == item.match; });
        records_queued_ = static_cast<std::size_t>(
            std::ranges::count_if(queue_, [](const Item& queued) { return queued.kind == Kind::kRecord; }));
        observer_->OnQueued(records_queued_);
        StopLocked(item.match, loss->step, std::move(loss->detail));
      }
      if (queue_.empty()) {
        written_.notify_all();
      }
    }
  }

  // Handle under the writer thread's boundary: an exception escaping it, from
  // the disk or anything else the writer calls, loses the capture as a failed
  // write does, where it would otherwise end the process.
  std::optional<Failed> HandleGuarded(const Item& item) {
    auto handled = failure::Guard(failure::Code::kCaptureWriteFailed, [&] { return Handle(item); });
    if (!handled.has_value()) {
      return Failed{.step = CaptureStep::kWrite, .detail = std::move(handled.error().detail)};
    }
    return *std::move(handled);
  }

  // Mechanism: carries out item on the writer's own file, or says why the
  // file failed. Writer thread only.
  std::optional<Failed> Handle(const Item& item) {
    switch (item.kind) {
      case Kind::kOpen:
        return OpenFile(item);
      case Kind::kRecord:
        return file_match_ == item.match ? Persist(*item.encoded) : std::nullopt;
      case Kind::kClose:
        CloseFile(item.match);
        return std::nullopt;
    }
    return std::nullopt;
  }

  // Makes room in the directory for the magic and the header, then creates
  // the file with them.
  std::optional<Failed> OpenFile(const Item& item) {
    if (!directory_.Open(item.match, utf8::FromPath(item.path.filename()), item.encoded->SizeOnDisk())) {
      return Failed{.step = CaptureStep::kRetentionBudget, .detail = {}};
    }
    file_ = std::ofstream(item.path, std::ios::binary | std::ios::trunc);
    if (!file_) {
      return Failed{.step = CaptureStep::kCreate, .detail = utf8::FromPath(item.path)};
    }
    file_match_ = item.match;
    path_ = item.path;
    records_ = 0;
    LOG_INFO("subsystem=capture event=capture_started match={} path={}", item.match, utf8::FromPath(path_));
    observer_->OnStarted();
    return Write(*item.encoded);
  }

  std::optional<std::string> Trip(failure::Site site) const {
    return faults_ == nullptr ? std::nullopt : faults_->Trip(site);
  }

  // Makes room in the directory for encoded, then writes it.
  std::optional<Failed> Persist(const capture::Encoded& encoded) {
    if (!directory_.Reserve(encoded.SizeOnDisk())) {
      return Failed{.step = CaptureStep::kRetentionBudget, .detail = {}};
    }
    return Write(encoded);
  }

  // Writes and flushes encoded, or says which step the file failed at. An
  // injected fault fails the file, as the disk would.
  std::optional<Failed> Write(const capture::Encoded& encoded) {
    if (std::optional<std::string> fault = Trip(failure::Site::kCaptureWrite)) {
      file_.setstate(std::ios::badbit);
      return Failed{.step = CaptureStep::kWrite, .detail = *std::move(fault)};
    }
    capture::Write(file_, encoded);
    if (!file_) {
      return Failed{.step = CaptureStep::kWrite, .detail = utf8::FromPath(path_)};
    }
    if (std::optional<std::string> fault = Trip(failure::Site::kCaptureFlush)) {
      file_.setstate(std::ios::badbit);
      return Failed{.step = CaptureStep::kFlush, .detail = *std::move(fault)};
    }
    file_.flush();
    if (!file_) {
      return Failed{.step = CaptureStep::kFlush, .detail = utf8::FromPath(path_)};
    }
    ++records_;
    observer_->OnWritten(encoded.SizeOnDisk());
    return std::nullopt;
  }

  void CloseFile(const uuid::Uuid& match) {
    bool stopped = false;
    {
      const std::scoped_lock lock(mutex_);
      // Nothing of match is queued past its close, so it is forgotten here.
      stopped = stopped_.erase(match) > 0;
    }
    if (file_match_ != match) {
      return;
    }
    file_.close();
    file_match_ = uuid::kNil;
    // A capture that stopped said so once already.
    if (!stopped) {
      LOG_INFO("subsystem=capture event=capture_completed match={} path={} records={}", match, utf8::FromPath(path_),
               records_);
      observer_->OnCompleted();
    }
  }

  // match's capture stops at step: reported once, with what the disk said of
  // it in detail, nothing more of it queued, and its file closed once the
  // writer gets to it. Told as the state of captures only while match is
  // still the one open. With mutex_ held.
  void StopLocked(const uuid::Uuid& match, CaptureStep step, std::string detail) {
    if (!stopped_.insert(match).second) {
      return;
    }
    const CaptureStop stop = StopAt(step);
    if (stop == CaptureStop::kWriteFailed) {
      LOG_WARN("subsystem=capture event=capture_stopped match={} reason={} step={} detail=\"{}\"", match,
               CaptureStopName(stop), CaptureStepName(step), detail);
    } else {
      LOG_WARN("subsystem=capture event=capture_stopped match={} reason={}", match, CaptureStopName(stop));
    }
    observer_->OnStopped(stop);
    if (match == open_match_) {
      observer_->OnState(CaptureState::kStopped);
    }
    LoseLocked(match, step, std::move(detail));
  }

  // The run's first loss, of match's record at step: kept, and reported once
  // as the health it leaves the captures in. A later one changes nothing.
  // With mutex_ held.
  void LoseLocked(const uuid::Uuid& match, CaptureStep step, std::string detail) {
    if (health_ != CaptureHealth::kEnabled) {
      return;
    }
    loss_ = LostCapture(mode_, match, step, std::move(detail));
    if (mode_ == CaptureMode::kOptional) {
      // This is the boundary that recovers it: the run goes on without it.
      LOG_ERROR("subsystem=capture event=capture_degraded mode={} {}", CaptureModeName(mode_),
                failure::DescribeFailure(*loss_));
      Enter(CaptureHealth::kDegraded);
    } else {
      // Only the state change: the runtime that stops on the failure writes
      // its one ERR line, with its detail (ADR-0033).
      LOG_INFO("subsystem=capture event=capture_disabled mode={} match={} step={}", CaptureModeName(mode_), match,
               CaptureStepName(step));
      Enter(CaptureHealth::kStopped);
    }
  }

  // The run's captures are over, logged once with whether any lost a record.
  // With mutex_ held.
  void StopCapturesLocked() {
    if (health_ == CaptureHealth::kStopped) {
      return;
    }
    LOG_INFO("subsystem=capture event=capture_disabled mode={} lost={}", CaptureModeName(mode_), loss_.has_value());
    Enter(CaptureHealth::kStopped);
  }

  void Enter(CaptureHealth health) {
    health_ = health;
    observer_->OnHealth(health);
  }

  const CaptureMode mode_;
  failure::Faults* const faults_;
  const std::size_t capacity_;
  // Told nothing, for a Capturer given no observer.
  CaptureObserver unobserved_;
  CaptureObserver* const observer_;
  // Kept within its retention by the writer thread alone.
  CaptureDirectory directory_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  // Notified each time the queue is found empty, its last item handled.
  std::condition_variable written_;
  std::deque<Item> queue_;
  std::size_t records_queued_ = 0;
  // The Matches whose capture stopped and whose file is not yet closed.
  std::unordered_set<uuid::Uuid> stopped_;
  // The Match the Simulation thread has open (nil for none), as Begin and Close set it.
  uuid::Uuid open_match_ = uuid::kNil;
  bool closing_ = false;
  // An item off the queue but not yet handled keeps WaitUntilWritten waiting.
  bool writing_ = false;
  // Written under mutex_, read without it by any thread.
  std::atomic<CaptureHealth> health_ = CaptureHealth::kEnabled;
  std::optional<failure::Failure> loss_;
  // The file open, the Match it captures (nil for none), and how many records
  // it holds. Writer thread only.
  std::ofstream file_;
  uuid::Uuid file_match_ = uuid::kNil;
  std::filesystem::path path_;
  std::size_t records_ = 0;
  // The directory's captures, as scanned at the file's creation and grown by
  // what was written since. Writer thread only.
  // Declared last, so it is joined before what it writes from goes.
  std::thread thread_;
};

Capturer::Capturer(std::filesystem::path directory, capture::Header header, CaptureOptions options)
    : directory_(std::move(directory)),
      header_(std::move(header)),
      writer_(std::make_unique<Writer>(directory_, std::move(options))) {}

Capturer::~Capturer() = default;

void Capturer::StartMatch(const uuid::Uuid& match, const std::vector<CaptureEntrant>& players, tick::Tick first_tick,
                          std::chrono::system_clock::time_point started) {
  if (open_) {
    writer_->Close(match_);
    open_ = false;
  }
  // A strict capture that lost a record captures nothing more.
  if (Health() == CaptureHealth::kStopped) {
    return;
  }
  match_ = match;
  open_ = true;
  writer_->Begin(match_);
  first_tick_ = first_tick;
  players_.clear();
  sessions_.clear();
  capture::Header header = header_;
  header.match = match_;
  header.started = std::chrono::floor<std::chrono::milliseconds>(started);
  std::optional<capture::Encoded> encoded = Admit(capture::EncodeHeader(header));
  if (!encoded.has_value()) {
    // No file is opened: its Joins and the rest are dropped by the writer.
    return;
  }
  writer_->Open(match_, directory_ / CaptureFileName(header.started, match_), *std::move(encoded));
  for (const CaptureEntrant& entrant : players) {
    const auto player = static_cast<capture::Player>(players_.size() + 1);
    players_.emplace(entrant.entity, player);
    sessions_.emplace(entrant.session, player);
    Queue(capture::EncodeJoin(player, capture::Join{.session = entrant.session,
                                                    .character = entrant.character,
                                                    .spawn = math::SnapPosition(entrant.spawn)}));
  }
}

void Capturer::Command(tick::Tick tick, EntityId entity, const command::Command& command) {
  const capture::Player player = PlayerOf(entity);
  if (!open_ || player == 0) {
    return;
  }
  const std::int64_t seen =
      std::clamp<std::int64_t>(Offset(first_tick_, command.seen_tick), std::numeric_limits<std::int32_t>::min(),
                               std::numeric_limits<std::int32_t>::max());
  command::Command captured = command;
  captured.seen_tick = 0;
  Queue(capture::EncodeEvent(
      {.offset = OffsetOf(tick),
       .event =
           capture::Command{.player = player, .seen_offset = static_cast<std::int32_t>(seen), .command = captured}}));
}

void Capturer::Leave(tick::Tick tick, EntityId entity) {
  const capture::Player player = PlayerOf(entity);
  if (!open_ || player == 0) {
    return;
  }
  Queue(capture::EncodeEvent({.offset = OffsetOf(tick), .event = capture::Leave{.player = player}}));
}

void Capturer::Death(tick::Tick tick, EntityId victim, EntityId killer) {
  const capture::Player dead = PlayerOf(victim);
  if (!open_ || dead == 0) {
    return;
  }
  Queue(capture::EncodeEvent(
      {.offset = OffsetOf(tick), .event = capture::Death{.victim = dead, .killer = PlayerOf(killer)}}));
}

void Capturer::EndMatch(tick::Tick last_tick, std::optional<SessionId> winner) {
  if (!open_) {
    return;
  }
  std::optional<capture::Player> winning;
  if (winner.has_value()) {
    if (const auto found = sessions_.find(*winner); found != sessions_.end()) {
      winning = found->second;
    }
  }
  Queue(capture::EncodeEvent({.offset = OffsetOf(last_tick), .event = capture::MatchEnd{.winner = winning}}));
  writer_->Close(match_);
  open_ = false;
  players_.clear();
  sessions_.clear();
}

void Capturer::WaitUntilWritten() { writer_->WaitUntilWritten(); }

void Capturer::Finish() {
  writer_->Finish();
  open_ = false;
}

CaptureHealth Capturer::Health() const { return writer_->Health(); }

std::optional<failure::Failure> Capturer::Loss() const {
  // The Simulation thread asks every tick: captures still enabled have lost
  // nothing, which the health says without the writer's lock.
  if (Health() == CaptureHealth::kEnabled) {
    return std::nullopt;
  }
  return writer_->Loss();
}

const std::optional<failure::Failure>& Capturer::Failure() const { return failure_; }

capture::Player Capturer::PlayerOf(EntityId entity) const {
  const auto found = players_.find(entity);
  return found == players_.end() ? capture::Player{0} : found->second;
}

std::uint32_t Capturer::OffsetOf(tick::Tick tick) const {
  return static_cast<std::uint32_t>(
      std::clamp<std::int64_t>(Offset(first_tick_, tick), 0, std::numeric_limits<std::uint32_t>::max()));
}

std::optional<capture::Encoded> Capturer::Admit(std::expected<capture::Encoded, capture::EncodeError> encoded) {
  if (encoded.has_value()) {
    return *std::move(encoded);
  }
  if (auto* broken = std::get_if<failure::Failure>(&encoded.error())) {
    // Not the capture's loss but the server's own bug, which stops the runtime
    // (ADR-0033): kept for Host to take, and nothing of it written.
    if (!failure_.has_value()) {
      failure_ = std::move(*broken);
    }
    return std::nullopt;
  }
  // A record capture::ReadCapture would refuse would make every record after
  // it unreadable; stopping here keeps the file readable up to it.
  writer_->Stop(match_, CaptureStep::kRecordTooLong);
  return std::nullopt;
}

void Capturer::Queue(std::expected<capture::Encoded, capture::EncodeError> encoded) {
  if (std::optional<capture::Encoded> admitted = Admit(std::move(encoded))) {
    writer_->Push(match_, *std::move(admitted));
  }
}

}  // namespace augusta::host
