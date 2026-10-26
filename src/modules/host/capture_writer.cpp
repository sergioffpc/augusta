#include "capture_writer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <ios>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "augusta/capture.h"
#include "augusta/capture_retention.h"
#include "augusta/capturer.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/logging.h"

namespace augusta::host {

namespace {

// path in UTF-8: path::string() converts to the ANSI code page on Windows, and
// throws for a character it lacks.
std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return {text.begin(), text.end()};
}

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

}  // namespace

Capturer::Writer::Writer(std::filesystem::path directory, CaptureOptions options)
    : mode_(options.mode),
      faults_(options.faults),
      capacity_(options.capacity),
      observer_(options.observer != nullptr ? options.observer : &unobserved_),
      directory_(std::move(directory), std::move(options.retention), RetentionReports(*observer_), options.faults) {
  observer_->OnState(CaptureState::kIdle);
  Enter(CaptureHealth::kEnabled);
  thread_ = std::thread([this] { Run(); });
}

Capturer::Writer::~Writer() {
  {
    const std::scoped_lock lock(mutex_);
    closing_ = true;
  }
  ready_.notify_one();
  thread_.join();
  const std::scoped_lock lock(mutex_);
  StopCapturesLocked();
}

void Capturer::Writer::Begin(const boost::uuids::uuid& match) {
  const std::scoped_lock lock(mutex_);
  open_match_ = match;
  observer_->OnState(CaptureState::kCapturing);
}

void Capturer::Writer::Open(const boost::uuids::uuid& match, std::filesystem::path path, capture::Encoded header) {
  Enqueue({.kind = Kind::kOpen, .match = match, .encoded = std::move(header), .path = std::move(path)});
}

void Capturer::Writer::Close(const boost::uuids::uuid& match) {
  {
    const std::scoped_lock lock(mutex_);
    if (open_match_ == match) {
      open_match_ = boost::uuids::nil_uuid();
      observer_->OnState(CaptureState::kIdle);
    }
    queue_.push_back({.kind = Kind::kClose, .match = match, .encoded = std::nullopt, .path = {}});
  }
  ready_.notify_one();
}

void Capturer::Writer::Push(const boost::uuids::uuid& match, capture::Encoded record) {
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

void Capturer::Writer::Stop(const boost::uuids::uuid& match, CaptureStep step) {
  const std::scoped_lock lock(mutex_);
  StopLocked(match, step, {});
}

void Capturer::Writer::WaitUntilWritten() {
  std::unique_lock lock(mutex_);
  written_.wait(lock, [this] { return queue_.empty() && !writing_; });
}

void Capturer::Writer::Finish() {
  std::unique_lock lock(mutex_);
  written_.wait(lock, [this] { return queue_.empty() && !writing_; });
  StopCapturesLocked();
}

CaptureHealth Capturer::Writer::Health() const { return health_; }

std::optional<failure::Failure> Capturer::Writer::Loss() const {
  const std::scoped_lock lock(mutex_);
  return loss_;
}

void Capturer::Writer::Enqueue(Item item) {
  {
    const std::scoped_lock lock(mutex_);
    queue_.push_back(std::move(item));
  }
  ready_.notify_one();
}

void Capturer::Writer::Run() {
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

std::optional<Capturer::Writer::Failed> Capturer::Writer::HandleGuarded(const Item& item) {
  auto handled = failure::Guard(failure::Code::kCaptureWriteFailed, [&] { return Handle(item); });
  if (!handled.has_value()) {
    return Failed{.step = CaptureStep::kWrite, .detail = std::move(handled.error().detail)};
  }
  return *std::move(handled);
}

std::optional<Capturer::Writer::Failed> Capturer::Writer::Handle(const Item& item) {
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

std::optional<Capturer::Writer::Failed> Capturer::Writer::OpenFile(const Item& item) {
  if (!directory_.Open(item.match, Utf8(item.path.filename()), item.encoded->SizeOnDisk())) {
    return Failed{.step = CaptureStep::kRetentionBudget, .detail = {}};
  }
  file_ = std::ofstream(item.path, std::ios::binary | std::ios::trunc);
  if (!file_) {
    return Failed{.step = CaptureStep::kCreate, .detail = Utf8(item.path)};
  }
  file_match_ = item.match;
  path_ = item.path;
  records_ = 0;
  LOG_INFO("subsystem=capture event=capture_started match={} path={}", boost::uuids::to_string(item.match),
           Utf8(path_));
  observer_->OnStarted();
  return Write(*item.encoded);
}

std::optional<std::string> Capturer::Writer::Trip(failure::Site site) const {
  return faults_ == nullptr ? std::nullopt : faults_->Trip(site);
}

std::optional<Capturer::Writer::Failed> Capturer::Writer::Persist(const capture::Encoded& encoded) {
  if (!directory_.Reserve(encoded.SizeOnDisk())) {
    return Failed{.step = CaptureStep::kRetentionBudget, .detail = {}};
  }
  return Write(encoded);
}

std::optional<Capturer::Writer::Failed> Capturer::Writer::Write(const capture::Encoded& encoded) {
  if (std::optional<std::string> fault = Trip(failure::Site::kCaptureWrite)) {
    file_.setstate(std::ios::badbit);
    return Failed{.step = CaptureStep::kWrite, .detail = *std::move(fault)};
  }
  capture::Write(file_, encoded);
  if (!file_) {
    return Failed{.step = CaptureStep::kWrite, .detail = Utf8(path_)};
  }
  if (std::optional<std::string> fault = Trip(failure::Site::kCaptureFlush)) {
    file_.setstate(std::ios::badbit);
    return Failed{.step = CaptureStep::kFlush, .detail = *std::move(fault)};
  }
  file_.flush();
  if (!file_) {
    return Failed{.step = CaptureStep::kFlush, .detail = Utf8(path_)};
  }
  ++records_;
  observer_->OnWritten(encoded.SizeOnDisk());
  return std::nullopt;
}

void Capturer::Writer::CloseFile(const boost::uuids::uuid& match) {
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
  file_match_ = boost::uuids::nil_uuid();
  // A capture that stopped said so once already.
  if (!stopped) {
    LOG_INFO("subsystem=capture event=capture_completed match={} path={} records={}", boost::uuids::to_string(match),
             Utf8(path_), records_);
    observer_->OnCompleted();
  }
}

void Capturer::Writer::StopLocked(const boost::uuids::uuid& match, CaptureStep step, std::string detail) {
  if (!stopped_.insert(match).second) {
    return;
  }
  const CaptureStop stop = StopAt(step);
  if (stop == CaptureStop::kWriteFailed) {
    LOG_WARN("subsystem=capture event=capture_stopped match={} reason={} step={} detail=\"{}\"",
             boost::uuids::to_string(match), CaptureStopName(stop), CaptureStepName(step), detail);
  } else {
    LOG_WARN("subsystem=capture event=capture_stopped match={} reason={}", boost::uuids::to_string(match),
             CaptureStopName(stop));
  }
  observer_->OnStopped(stop);
  if (match == open_match_) {
    observer_->OnState(CaptureState::kStopped);
  }
  LoseLocked(match, step, std::move(detail));
}

void Capturer::Writer::LoseLocked(const boost::uuids::uuid& match, CaptureStep step, std::string detail) {
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
    LOG_INFO("subsystem=capture event=capture_disabled mode={} match={} step={}", CaptureModeName(mode_),
             boost::uuids::to_string(match), CaptureStepName(step));
    Enter(CaptureHealth::kStopped);
  }
}

void Capturer::Writer::StopCapturesLocked() {
  if (health_ == CaptureHealth::kStopped) {
    return;
  }
  LOG_INFO("subsystem=capture event=capture_disabled mode={} lost={}", CaptureModeName(mode_), loss_.has_value());
  Enter(CaptureHealth::kStopped);
}

void Capturer::Writer::Enter(CaptureHealth health) {
  health_ = health;
  observer_->OnHealth(health);
}

}  // namespace augusta::host
