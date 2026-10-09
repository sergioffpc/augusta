#include "capture.h"

#include <algorithm>
#include <array>
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
#include <istream>
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

#include "augusta/capture_error.h"
#include "augusta/capture_file.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/grid.h"
#include "augusta/logging.h"
#include "augusta/protocol.h"
#include "augusta/tick.h"
#include "capture_retention.h"
#include "match.h"
#include "wire.h"

namespace augusta::server {

namespace {

// What every capture's file name ends in (CaptureFileName).
constexpr std::string_view kCaptureExtension = ".capture";

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

// How many ticks to is after from: negative when it is before.
std::int64_t Offset(tick::Tick from, tick::Tick to) {
  return static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
}

}  // namespace

std::expected<Capture, capture_file::ReadError> ReadCapture(std::istream& in) {
  auto file = capture_file::ReadCaptureFile(in);
  if (!file.has_value()) {
    return std::unexpected(file.error());
  }
  Capture capture{.header = FromWire(file->header), .records = {}, .torn = file->torn};
  capture.records.reserve(file->records.size());
  for (const protocol::CaptureRecordWire& wire : file->records) {
    // The file's order admits no header past the first, which alone gives none.
    if (std::optional<CaptureRecord> record = FromWire(wire)) {
      capture.records.push_back(*std::move(record));
    }
  }
  return capture;
}

std::string CaptureFileName(std::chrono::sys_time<std::chrono::milliseconds> started, std::uint64_t match_number) {
  const auto seconds = std::chrono::floor<std::chrono::seconds>(started);
  return std::format("{:%Y%m%dT%H%M%S}{:03}Z-{:04}{}", seconds, (started - seconds).count(), match_number,
                     kCaptureExtension);
}

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

// The thread a Capturer writes its files on and what is queued for it, oldest
// first: each Match's file opened, its records, and its file closed, each
// item naming its Match by its number in the run. The Simulation thread only
// ever waits on the lock, which the writer holds to take an item and never
// across a write. It tells its observer what it does, the state of captures
// under the lock so that a stop the writer finds is never told after the
// Simulation thread has moved to another Match.
class Capturer::Writer {
 public:
  Writer(std::filesystem::path directory, CaptureOptions options)
      : faults_(options.faults),
        capacity_(options.capacity),
        observer_(options.observer != nullptr ? options.observer : &unobserved_),
        directory_(std::move(directory), std::move(options.retention), RetentionReports(*observer_), options.faults) {
    observer_->OnState(CaptureState::kIdle);
    thread_ = std::thread([this] { Run(); });
  }

  // Writes what is still queued before it goes.
  ~Writer() {
    {
      const std::scoped_lock lock(mutex_);
      closing_ = true;
    }
    ready_.notify_one();
    thread_.join();
  }

  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;
  Writer(Writer&&) = delete;
  Writer& operator=(Writer&&) = delete;

  // match is the Match open, captured until its Close or a stop.
  void Begin(std::uint64_t match) {
    const std::scoped_lock lock(mutex_);
    open_match_ = match;
    observer_->OnState(CaptureState::kCapturing);
  }

  void Open(std::uint64_t match, std::filesystem::path path, protocol::BytesWire header) {
    Enqueue({.kind = Kind::kOpen, .match = match, .payload = std::move(header), .path = std::move(path)});
  }

  void Close(std::uint64_t match) {
    {
      const std::scoped_lock lock(mutex_);
      if (open_match_ == match) {
        open_match_ = 0;
        observer_->OnState(CaptureState::kIdle);
      }
      queue_.push_back({.kind = Kind::kClose, .match = match, .payload = {}, .path = {}});
    }
    ready_.notify_one();
  }

  // Queues match's record, or stops its capture there when capacity records
  // are still unwritten. Once its capture has stopped, drops every one.
  void Push(std::uint64_t match, protocol::BytesWire record) {
    {
      const std::scoped_lock lock(mutex_);
      if (stopped_.contains(match)) {
        return;
      }
      // Dropping this record and going on would leave a capture missing one
      // in its middle; stopping here keeps every one before it.
      if (records_queued_ >= capacity_) {
        StopLocked(match, CaptureStop::kQueueFull);
        return;
      }
      ++records_queued_;
      observer_->OnQueued(records_queued_);
      queue_.push_back({.kind = Kind::kRecord, .match = match, .payload = std::move(record), .path = {}});
    }
    ready_.notify_one();
  }

  void Stop(std::uint64_t match, CaptureStop stop) {
    const std::scoped_lock lock(mutex_);
    StopLocked(match, stop);
  }

  void WaitUntilWritten() {
    std::unique_lock lock(mutex_);
    written_.wait(lock, [this] { return queue_.empty() && !writing_; });
  }

 private:
  enum class Kind : std::uint8_t { kOpen, kRecord, kClose };

  // Where the writer lost a Match's file, as its capture_stopped line names it.
  enum class Step : std::uint8_t { kCreate, kWrite };

  // Why the writer stopped a Match's capture, and what it knows of where.
  struct Loss {
    CaptureStop stop;
    std::string where;
  };

  static Loss Failed(Step step, std::string_view detail) {
    return Loss{.stop = CaptureStop::kWriteFailed,
                .where = std::format("step={} detail=\"{}\"", step == Step::kCreate ? "create" : "write", detail)};
  }

  struct Item {
    Kind kind;
    std::uint64_t match;
    protocol::BytesWire payload;
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
      std::optional<Loss> loss = Handle(item);
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
        StopLocked(item.match, loss->stop, loss->where);
      }
      if (queue_.empty()) {
        written_.notify_all();
      }
    }
  }

  // Mechanism: carries out item on the writer's own file, or says why the
  // file failed. Writer thread only.
  std::optional<Loss> Handle(const Item& item) {
    switch (item.kind) {
      case Kind::kOpen:
        return OpenFile(item);
      case Kind::kRecord:
        return file_match_ == item.match ? Persist(item.payload) : std::nullopt;
      case Kind::kClose:
        CloseFile(item.match);
        return std::nullopt;
    }
    return std::nullopt;
  }

  // Makes room in the directory for the magic and the header, then creates
  // the file with them.
  std::optional<Loss> OpenFile(const Item& item) {
    if (!directory_.Open(item.match, item.path.filename().string(),
                         protocol::kCaptureMagic.size() +
                             capture_file::FrameSize(capture_file::kCaptureFrames, item.payload.size()))) {
      return Loss{.stop = CaptureStop::kRetentionBudget, .where = {}};
    }
    file_ = std::ofstream(item.path, std::ios::binary | std::ios::trunc);
    if (!file_) {
      return Failed(Step::kCreate, item.path.string());
    }
    file_match_ = item.match;
    path_ = item.path;
    records_ = 0;
    file_.write(reinterpret_cast<const char*>(protocol::kCaptureMagic.data()), protocol::kCaptureMagic.size());
    LI("subsystem=capture event=capture_started match={} path={}", item.match, path_.string());
    observer_->OnStarted();
    if (file_) {
      observer_->OnWritten(protocol::kCaptureMagic.size());
    }
    return Write(item.payload);
  }

  // Makes room in the directory for payload's frame, then writes it.
  std::optional<Loss> Persist(const protocol::BytesWire& payload) {
    if (!directory_.Reserve(capture_file::FrameSize(capture_file::kCaptureFrames, payload.size()))) {
      return Loss{.stop = CaptureStop::kRetentionBudget, .where = {}};
    }
    return Write(payload);
  }

  // Writes and flushes payload's frame. An injected fault fails the file, as the disk would.
  std::optional<Loss> Write(const protocol::BytesWire& payload) {
    if (std::optional<std::string> fault =
            faults_ == nullptr ? std::nullopt : faults_->Trip(failure::Site::kCaptureWrite)) {
      file_.setstate(std::ios::badbit);
      return Failed(Step::kWrite, *fault);
    }
    capture_file::WriteFrame(file_, capture_file::kCaptureFrames, payload);
    file_.flush();
    if (!file_) {
      return Failed(Step::kWrite, path_.string());
    }
    ++records_;
    observer_->OnWritten(capture_file::FrameSize(capture_file::kCaptureFrames, payload.size()));
    return std::nullopt;
  }

  void CloseFile(std::uint64_t match) {
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
    file_match_ = 0;
    // A capture that stopped said so once already.
    if (!stopped) {
      LI("subsystem=capture event=capture_completed match={} path={} records={}", match, path_.string(), records_);
      observer_->OnCompleted();
    }
  }

  // match's capture stops at stop: reported once, with what the writer knows
  // of it in where, nothing more of it queued, and its file closed once the
  // writer gets to it. Told as the state of captures only while match is
  // still the one open. With mutex_ held.
  void StopLocked(std::uint64_t match, CaptureStop stop, std::string_view where = {}) {
    if (!stopped_.insert(match).second) {
      return;
    }
    LW("subsystem=capture event=capture_stopped match={} reason={}{}{}", match, CaptureStopName(stop),
       where.empty() ? "" : " ", where);
    observer_->OnStopped(stop);
    if (match == open_match_) {
      observer_->OnState(CaptureState::kStopped);
    }
  }

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
  std::unordered_set<std::uint64_t> stopped_;
  // The Match the Simulation thread has open (0 for none), as Begin and Close set it.
  std::uint64_t open_match_ = 0;
  bool closing_ = false;
  // An item off the queue but not yet handled keeps WaitUntilWritten waiting.
  bool writing_ = false;
  // The file open, the Match it captures (0 for none), and how many records
  // it holds. Writer thread only.
  std::ofstream file_;
  std::uint64_t file_match_ = 0;
  std::filesystem::path path_;
  std::size_t records_ = 0;
  // The directory's captures, as scanned at the file's creation and grown by
  // what was written since. Writer thread only.
  // Declared last, so it is joined before what it writes from goes.
  std::thread thread_;
};

Capturer::Capturer(std::filesystem::path directory, CaptureHeader header, CaptureOptions options)
    : directory_(std::move(directory)),
      header_(std::move(header)),
      writer_(std::make_unique<Writer>(directory_, std::move(options))) {}

Capturer::~Capturer() = default;

void Capturer::StartMatch(const std::vector<CaptureEntrant>& players, tick::Tick first_tick,
                          std::chrono::system_clock::time_point started) {
  if (open_) {
    writer_->Close(matches_);
  }
  ++matches_;
  open_ = true;
  writer_->Begin(matches_);
  first_tick_ = first_tick;
  players_.clear();
  sessions_.clear();
  CaptureHeader header = header_;
  header.started = std::chrono::floor<std::chrono::milliseconds>(started);
  std::optional<protocol::BytesWire> encoded = Admit(EncodeToCapture(ToWire(header)));
  if (!encoded.has_value()) {
    // No file is opened: its Joins and the rest are dropped by the writer.
    return;
  }
  writer_->Open(matches_, directory_ / CaptureFileName(header.started, matches_), *std::move(encoded));
  for (const CaptureEntrant& entrant : players) {
    const auto player = static_cast<CapturedPlayer>(players_.size() + 1);
    players_.emplace(entrant.entity, player);
    sessions_.emplace(entrant.session, player);
    Queue(CaptureRecord{.offset = 0,
                        .event = CapturedJoin{.player = player,
                                              .session = entrant.session,
                                              .character = entrant.character,
                                              .spawn = math::SnapPosition(entrant.spawn)}});
  }
}

void Capturer::Command(tick::Tick tick, EntityId entity, const command::Command& command) {
  const CapturedPlayer player = PlayerOf(entity);
  if (!open_ || player == 0) {
    return;
  }
  const std::int64_t seen =
      std::clamp<std::int64_t>(Offset(first_tick_, command.seen_tick), std::numeric_limits<std::int32_t>::min(),
                               std::numeric_limits<std::int32_t>::max());
  command::Command captured = command;
  captured.seen_tick = 0;
  Queue(CaptureRecord{
      .offset = OffsetOf(tick),
      .event = CapturedCommand{.player = player, .seen_offset = static_cast<std::int32_t>(seen), .command = captured}});
}

void Capturer::Leave(tick::Tick tick, EntityId entity) {
  const CapturedPlayer player = PlayerOf(entity);
  if (!open_ || player == 0) {
    return;
  }
  Queue(CaptureRecord{.offset = OffsetOf(tick), .event = CapturedLeave{.player = player}});
}

void Capturer::Death(tick::Tick tick, EntityId victim, EntityId killer) {
  const CapturedPlayer dead = PlayerOf(victim);
  if (!open_ || dead == 0) {
    return;
  }
  Queue(CaptureRecord{.offset = OffsetOf(tick), .event = CapturedDeath{.victim = dead, .killer = PlayerOf(killer)}});
}

void Capturer::EndMatch(tick::Tick last_tick, std::optional<SessionId> winner) {
  if (!open_) {
    return;
  }
  std::optional<CapturedPlayer> winning;
  if (winner.has_value()) {
    if (const auto found = sessions_.find(*winner); found != sessions_.end()) {
      winning = found->second;
    }
  }
  Queue(CaptureRecord{.offset = OffsetOf(last_tick), .event = CapturedMatchEnd{.winner = winning}});
  writer_->Close(matches_);
  open_ = false;
  players_.clear();
  sessions_.clear();
}

void Capturer::WaitUntilWritten() { writer_->WaitUntilWritten(); }

const std::optional<failure::Failure>& Capturer::Failure() const { return failure_; }

CapturedPlayer Capturer::PlayerOf(EntityId entity) const {
  const auto found = players_.find(entity);
  return found == players_.end() ? CapturedPlayer{0} : found->second;
}

std::uint32_t Capturer::OffsetOf(tick::Tick tick) const {
  return static_cast<std::uint32_t>(
      std::clamp<std::int64_t>(Offset(first_tick_, tick), 0, std::numeric_limits<std::uint32_t>::max()));
}

std::optional<std::vector<std::byte>> Capturer::Admit(std::expected<std::vector<std::byte>, failure::Failure> encoded) {
  // Not the capture's loss but the server's own bug, which stops the runtime
  // (ADR-0033): kept for Host to take, and nothing of it written.
  if (!encoded.has_value()) {
    if (!failure_.has_value()) {
      failure_ = std::move(encoded.error());
    }
    return std::nullopt;
  }
  // A record ReadCapture would refuse would make every record after it
  // unreadable; stopping here keeps the file readable up to it.
  if (encoded->size() > capture_file::kCaptureFrames.max_payload) {
    writer_->Stop(matches_, CaptureStop::kRecordTooLong);
    return std::nullopt;
  }
  return *std::move(encoded);
}

void Capturer::Queue(const CaptureRecord& record) {
  if (std::optional<protocol::BytesWire> encoded = Admit(EncodeToCapture(ToWire(record)))) {
    writer_->Push(matches_, *std::move(encoded));
  }
}

}  // namespace augusta::server
