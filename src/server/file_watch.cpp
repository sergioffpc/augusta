#include "file_watch.h"

#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <system_error>
#include <thread>
#include <utility>

#include "augusta/logging.h"

namespace augusta::server {

std::optional<FileSignature> Look(const std::filesystem::path& file) {
  std::error_code error;
  std::filesystem::path target = std::filesystem::canonical(file, error);
  if (error) {
    return std::nullopt;
  }
  const auto modified = std::filesystem::last_write_time(target, error);
  if (error) {
    return std::nullopt;
  }
  const auto size = std::filesystem::file_size(target, error);
  if (error) {
    return std::nullopt;
  }
  return FileSignature{.target = std::move(target), .modified = modified, .size = size};
}

ChangeDetector::ChangeDetector(std::optional<FileSignature> baseline, std::chrono::milliseconds debounce)
    : debounce_(debounce), last_seen_(baseline), acted_on_(std::move(baseline)) {}

WatchEvent ChangeDetector::Feed(const std::optional<FileSignature>& seen, std::chrono::steady_clock::time_point now) {
  if (seen != last_seen_) {
    last_seen_ = seen;
    seen_since_ = now;
  }
  if (last_seen_ == acted_on_ || now - seen_since_ < debounce_) {
    return WatchEvent::kNone;
  }
  acted_on_ = last_seen_;
  return last_seen_.has_value() ? WatchEvent::kChanged : WatchEvent::kLost;
}

struct FileWatcher::Impl {
  std::filesystem::path file;
  WatchOptions options;
  std::function<void()> on_change;
  ChangeDetector detector;

  std::mutex mutex;
  std::condition_variable_any wake;
  std::jthread thread;

  Impl(std::filesystem::path watched, const WatchOptions& watch_options, std::function<void()> callback)
      : file(std::move(watched)),
        options(watch_options),
        on_change(std::move(callback)),
        detector(Look(file), watch_options.debounce) {}

  void Poll() {
    switch (detector.Feed(Look(file), std::chrono::steady_clock::now())) {
      case WatchEvent::kNone:
        break;
      case WatchEvent::kChanged:
        LI("subsystem=serverruntime event=watch_changed path={}", file.string());
        on_change();
        break;
      case WatchEvent::kLost:
        LW("subsystem=serverruntime event=watch_lost path={} reason=\"the file cannot be observed\"", file.string());
        break;
    }
  }

  // Polls until stopped, waiting between polls on a condition the destructor
  // signals, so stopping does not wait out an interval.
  void Run(const std::stop_token& stop) {
    std::unique_lock<std::mutex> lock(mutex);
    while (!stop.stop_requested()) {
      lock.unlock();
      Poll();
      lock.lock();
      wake.wait_for(lock, stop, options.poll_interval, [] { return false; });
    }
  }
};

FileWatcher::FileWatcher(std::filesystem::path file, const WatchOptions& options, std::function<void()> on_change)
    : impl_(std::make_unique<Impl>(std::move(file), options, std::move(on_change))) {
  Impl& impl = *impl_;
  impl.thread = std::jthread([&impl](const std::stop_token& stop) { impl.Run(stop); });
}

// Stopping the jthread requests the stop and joins; the wait above sees the
// request through its stop_token and returns at once.
FileWatcher::~FileWatcher() = default;

}  // namespace augusta::server
