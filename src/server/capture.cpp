#include "capture.h"

#include <algorithm>
#include <array>
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
#include <functional>
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

#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/grid.h"
#include "augusta/logging.h"
#include "augusta/protocol.h"
#include "augusta/tick.h"
#include "frames.h"
#include "match.h"
#include "wire.h"

namespace augusta::server {

namespace {

// Where a capture lost a record: the step its capture_stopped line and its
// Failure name.
enum class Step : std::uint8_t {
  kCreate,
  kWrite,
  kFlush,
  // The record found the writer's queue full: the disk is not keeping up.
  kQueueFull,
  // The record is longer than a capture's frame holds.
  kRecordTooLong,
};

std::string_view StepName(Step step) {
  switch (step) {
    case Step::kCreate:
      return "create";
    case Step::kWrite:
      return "write";
    case Step::kFlush:
      return "flush";
    case Step::kQueueFull:
      return "queue_full";
    case Step::kRecordTooLong:
      return "record_too_long";
  }
  return "unknown";
}

// Why a capture lost at step stopped, as its capture_stopped line names it.
CaptureStop StopAt(Step step) {
  switch (step) {
    case Step::kQueueFull:
      return CaptureStop::kQueueFull;
    case Step::kRecordTooLong:
      return CaptureStop::kRecordTooLong;
    case Step::kCreate:
    case Step::kWrite:
    case Step::kFlush:
      return CaptureStop::kWriteFailed;
  }
  return CaptureStop::kWriteFailed;
}

// Decision: what losing match's record at step is in mode. An optional
// capture's loss is its own subsystem's; a strict one's is the runtime's.
failure::Failure LostCapture(CaptureMode mode, std::uint64_t match, Step step, std::string detail) {
  failure::Code code = failure::Code::kStrictCaptureFailed;
  if (mode == CaptureMode::kOptional) {
    code = step == Step::kFlush ? failure::Code::kCaptureFlushFailed : failure::Code::kCaptureWriteFailed;
  }
  return {.code = code,
          .context = {{.key = "match", .value = std::to_string(match)},
                      {.key = "step", .value = std::string(StepName(step))}},
          .detail = std::move(detail)};
}

// Decision: whether next may follow the records of capture so far, joins of
// them Joins (ADR-0050): every Join first, numbered from 1 in order, then the
// events in tick order, each naming a player a Join did, and nothing after
// the Match end.
bool Follows(const Capture& capture, std::size_t joins, const CaptureRecord& next) {
  if (!capture.records.empty()) {
    const CaptureRecord& last = capture.records.back();
    if (next.offset < last.offset || std::holds_alternative<CapturedMatchEnd>(last.event)) {
      return false;
    }
  }
  const auto known = [joins](CapturedPlayer player) { return player >= 1 && player <= joins; };
  if (const auto* join = std::get_if<CapturedJoin>(&next.event)) {
    return joins == capture.records.size() && join->player == joins + 1 && next.offset == 0;
  }
  if (const auto* command = std::get_if<CapturedCommand>(&next.event)) {
    return known(command->player);
  }
  if (const auto* leave = std::get_if<CapturedLeave>(&next.event)) {
    return known(leave->player);
  }
  if (const auto* death = std::get_if<CapturedDeath>(&next.event)) {
    return known(death->victim) && known(death->killer);
  }
  const auto& end = std::get<CapturedMatchEnd>(next.event);
  return !end.winner.has_value() || known(*end.winner);
}

// Reads a capture's magic off the front of in.
std::expected<void, CaptureError> ReadMagic(std::istream& in) {
  std::array<char, protocol::kCaptureMagic.size()> magic{};
  in.read(magic.data(), magic.size());
  if (in.bad()) {
    return std::unexpected(CaptureError::kUnreadable);
  }
  if (static_cast<std::size_t>(in.gcount()) != magic.size() ||
      !std::ranges::equal(magic, protocol::kCaptureMagic,
                          [](char read, std::byte expected) { return static_cast<std::byte>(read) == expected; })) {
    return std::unexpected(CaptureError::kNotACapture);
  }
  return {};
}

std::expected<CaptureHeader, CaptureError> ReadHeader(std::istream& in) {
  protocol::BytesWire payload;
  const Frame frame = ReadFrame(in, kCaptureFrames, payload);
  if (frame == Frame::kUnreadable) {
    return std::unexpected(CaptureError::kUnreadable);
  }
  if (frame != Frame::kRead) {
    return std::unexpected(CaptureError::kUnsupported);
  }
  const auto record = protocol::DecodeCaptureRecord(payload);
  const auto* header = record.has_value() ? std::get_if<protocol::CaptureHeaderWire>(&*record) : nullptr;
  if (header == nullptr || header->format_version != protocol::kCaptureFormatVersion) {
    return std::unexpected(CaptureError::kUnsupported);
  }
  return FromWire(*header);
}

// How many ticks to is after from: negative when it is before.
std::int64_t Offset(tick::Tick from, tick::Tick to) {
  return static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
}

}  // namespace

std::string_view DescribeCaptureError(CaptureError error) {
  switch (error) {
    case CaptureError::kUnreadable:
      return "the capture could not be read";
    case CaptureError::kNotACapture:
      return "the file does not start with a capture's magic";
    case CaptureError::kUnsupported:
      return "the capture's header is missing or of a format version this engine does not read";
    case CaptureError::kMalformed:
      return "a record of the capture does not decode or is out of order";
  }
  return "unknown capture error";
}

std::expected<Capture, CaptureError> ReadCapture(std::istream& in) {
  if (!in) {
    return std::unexpected(CaptureError::kUnreadable);
  }
  if (auto magic = ReadMagic(in); !magic.has_value()) {
    return std::unexpected(magic.error());
  }
  auto header = ReadHeader(in);
  if (!header.has_value()) {
    return std::unexpected(header.error());
  }
  Capture capture{.header = *std::move(header), .records = {}, .torn = false};
  std::size_t joins = 0;
  protocol::BytesWire payload;
  for (Frame frame = ReadFrame(in, kCaptureFrames, payload); frame != Frame::kEnd;
       frame = ReadFrame(in, kCaptureFrames, payload)) {
    if (frame == Frame::kUnreadable) {
      return std::unexpected(CaptureError::kUnreadable);
    }
    if (frame == Frame::kTorn) {
      capture.torn = true;
      break;
    }
    if (frame == Frame::kTooLong) {
      return std::unexpected(CaptureError::kMalformed);
    }
    const auto wire = protocol::DecodeCaptureRecord(payload);
    std::optional<CaptureRecord> record = wire.has_value() ? FromWire(*wire) : std::nullopt;
    if (!record.has_value() || !Follows(capture, joins, *record)) {
      return std::unexpected(CaptureError::kMalformed);
    }
    joins += std::holds_alternative<CapturedJoin>(record->event) ? 1 : 0;
    capture.records.push_back(*std::move(record));
  }
  return capture;
}

std::string CaptureFileName(std::chrono::sys_time<std::chrono::milliseconds> started, std::uint64_t match_number) {
  const auto seconds = std::chrono::floor<std::chrono::seconds>(started);
  return std::format("{:%Y%m%dT%H%M%S}{:03}Z-{:04}.capture", seconds, (started - seconds).count(), match_number);
}

std::string_view CaptureStopName(CaptureStop stop) {
  switch (stop) {
    case CaptureStop::kRecordTooLong:
      return "record_too_long";
    case CaptureStop::kQueueFull:
      return "queue_full";
    case CaptureStop::kWriteFailed:
      return "write_failed";
  }
  return "unknown";
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
// item naming its Match by its number in the run. The Simulation thread only
// ever waits on the lock, which the writer holds to take an item and never
// across a write. It keeps the run's first loss and the health it leaves the
// captures in.
class Capturer::Writer {
 public:
  explicit Writer(CaptureOptions options)
      : mode_(options.mode),
        faults_(options.faults),
        on_health_(std::move(options.on_health)),
        capacity_(options.capacity) {
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
    if (health_ != CaptureHealth::kStopped) {
      LI("subsystem=capture event=capture_disabled mode={} lost={}", CaptureModeName(mode_), loss_.has_value());
      Enter(CaptureHealth::kStopped);
    }
  }

  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;
  Writer(Writer&&) = delete;
  Writer& operator=(Writer&&) = delete;

  void Open(std::uint64_t match, std::filesystem::path path, protocol::BytesWire header) {
    Enqueue({.kind = Kind::kOpen, .match = match, .payload = std::move(header), .path = std::move(path)});
  }

  void Close(std::uint64_t match) { Enqueue({.kind = Kind::kClose, .match = match, .payload = {}, .path = {}}); }

  // Queues match's record, or stops its capture there when capacity records
  // are still unwritten. Once its capture has stopped, drops every one.
  void Push(std::uint64_t match, protocol::BytesWire record) {
    {
      const std::scoped_lock lock(mutex_);
      if (stopped_.contains(match) || health_ == CaptureHealth::kStopped) {
        return;
      }
      // Dropping this record and going on would leave a capture missing one
      // in its middle; stopping here keeps every one before it.
      if (records_queued_ >= capacity_) {
        StopLocked(match, Step::kQueueFull, {});
        return;
      }
      ++records_queued_;
      queue_.push_back({.kind = Kind::kRecord, .match = match, .payload = std::move(record), .path = {}});
    }
    ready_.notify_one();
  }

  void Stop(std::uint64_t match, Step step) {
    const std::scoped_lock lock(mutex_);
    StopLocked(match, step, {});
  }

  void WaitUntilWritten() {
    std::unique_lock lock(mutex_);
    written_.wait(lock, [this] { return queue_.empty() && !writing_; });
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
    Step step;
    std::string detail;
  };

  struct Item {
    Kind kind;
    std::uint64_t match;
    protocol::BytesWire payload;
    std::filesystem::path path;
  };

  // Queues an item that opens or closes a file: never refused, since each is
  // one per Match.
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
      }
      writing_ = true;
      lock.unlock();
      std::optional<Failed> loss = Handle(item);
      lock.lock();
      writing_ = false;
      if (loss.has_value()) {
        // Whatever of the record reached the file reads back as a torn last
        // record; writing on would put whole records after it no reader reaches.
        std::erase_if(queue_,
                      [&](const Item& queued) { return queued.kind == Kind::kRecord && queued.match == item.match; });
        records_queued_ = static_cast<std::size_t>(
            std::ranges::count_if(queue_, [](const Item& queued) { return queued.kind == Kind::kRecord; }));
        StopLocked(item.match, loss->step, std::move(loss->detail));
      }
      if (queue_.empty()) {
        written_.notify_all();
      }
    }
  }

  // Mechanism: carries out item on the writer's own file, or says why the
  // file failed. Writer thread only.
  std::optional<Failed> Handle(const Item& item) {
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

  std::optional<Failed> OpenFile(const Item& item) {
    file_ = std::ofstream(item.path, std::ios::binary | std::ios::trunc);
    if (!file_) {
      return Failed{.step = Step::kCreate, .detail = item.path.string()};
    }
    file_match_ = item.match;
    path_ = item.path;
    records_ = 0;
    file_.write(reinterpret_cast<const char*>(protocol::kCaptureMagic.data()), protocol::kCaptureMagic.size());
    LI("subsystem=capture event=capture_started match={} path={}", item.match, path_.string());
    return Persist(item.payload);
  }

  std::optional<std::string> Trip(failure::Site site) const {
    return faults_ == nullptr ? std::nullopt : faults_->Trip(site);
  }

  // Writes and flushes payload's frame, or says which step the file failed
  // at. An injected fault fails the file, as the disk would.
  std::optional<Failed> Persist(const protocol::BytesWire& payload) {
    if (std::optional<std::string> fault = Trip(failure::Site::kCaptureWrite)) {
      file_.setstate(std::ios::badbit);
      return Failed{.step = Step::kWrite, .detail = *std::move(fault)};
    }
    WriteFrame(file_, kCaptureFrames, payload);
    if (!file_) {
      return Failed{.step = Step::kWrite, .detail = path_.string()};
    }
    if (std::optional<std::string> fault = Trip(failure::Site::kCaptureFlush)) {
      file_.setstate(std::ios::badbit);
      return Failed{.step = Step::kFlush, .detail = *std::move(fault)};
    }
    file_.flush();
    if (!file_) {
      return Failed{.step = Step::kFlush, .detail = path_.string()};
    }
    ++records_;
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
    }
  }

  // match's capture stops at step: reported once, with what the disk said of
  // it in detail, nothing more of it queued, and its file closed once the
  // writer gets to it. With mutex_ held.
  void StopLocked(std::uint64_t match, Step step, std::string detail) {
    if (!stopped_.insert(match).second) {
      return;
    }
    const CaptureStop stop = StopAt(step);
    if (stop == CaptureStop::kWriteFailed) {
      LW("subsystem=capture event=capture_stopped match={} reason={} step={} detail=\"{}\"", match,
         CaptureStopName(stop), StepName(step), detail);
    } else {
      LW("subsystem=capture event=capture_stopped match={} reason={}", match, CaptureStopName(stop));
    }
    LoseLocked(match, step, std::move(detail));
  }

  // The run's first loss, of match's record at step: kept, and reported once
  // as the health it leaves the captures in. A later one changes nothing.
  // With mutex_ held.
  void LoseLocked(std::uint64_t match, Step step, std::string detail) {
    if (health_ != CaptureHealth::kEnabled) {
      return;
    }
    loss_ = LostCapture(mode_, match, step, std::move(detail));
    if (mode_ == CaptureMode::kOptional) {
      // This is the boundary that recovers it: the run goes on without it.
      LE("subsystem=capture event=capture_degraded mode={} {}", CaptureModeName(mode_),
         failure::DescribeFailure(*loss_));
      Enter(CaptureHealth::kDegraded);
    } else {
      // Only the state change: the runtime that stops on the failure writes
      // its one ERR line, with its detail (ADR-0033).
      LI("subsystem=capture event=capture_disabled mode={} match={} step={}", CaptureModeName(mode_), match,
         StepName(step));
      Enter(CaptureHealth::kStopped);
    }
  }

  void Enter(CaptureHealth health) {
    health_ = health;
    if (on_health_) {
      on_health_(health);
    }
  }

  const CaptureMode mode_;
  failure::Faults* const faults_;
  const std::function<void(CaptureHealth)> on_health_;
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  // Notified each time the queue is found empty, its last item handled.
  std::condition_variable written_;
  std::deque<Item> queue_;
  std::size_t records_queued_ = 0;
  // The Matches whose capture stopped and whose file is not yet closed.
  std::unordered_set<std::uint64_t> stopped_;
  bool closing_ = false;
  // An item off the queue but not yet handled keeps WaitUntilWritten waiting.
  bool writing_ = false;
  // Written under mutex_, read without it by any thread.
  std::atomic<CaptureHealth> health_ = CaptureHealth::kEnabled;
  std::optional<failure::Failure> loss_;
  // The file open, the Match it captures (0 for none), and how many records
  // it holds. Writer thread only.
  std::ofstream file_;
  std::uint64_t file_match_ = 0;
  std::filesystem::path path_;
  std::size_t records_ = 0;
  // Declared last, so it is joined before what it writes from goes.
  std::thread thread_;
};

Capturer::Capturer(std::filesystem::path directory, CaptureHeader header, CaptureOptions options)
    : directory_(std::move(directory)), header_(std::move(header)), writer_(std::make_unique<Writer>(options)) {}

Capturer::~Capturer() = default;

void Capturer::StartMatch(const std::vector<CaptureEntrant>& players, tick::Tick first_tick,
                          std::chrono::system_clock::time_point started) {
  if (open_) {
    writer_->Close(matches_);
    open_ = false;
  }
  // A strict capture that lost a record captures nothing more.
  if (Health() == CaptureHealth::kStopped) {
    return;
  }
  ++matches_;
  open_ = true;
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
  if (encoded->size() > kCaptureFrames.max_payload) {
    writer_->Stop(matches_, Step::kRecordTooLong);
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
