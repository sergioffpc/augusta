#ifndef AUGUSTA_SERVER_FILE_WATCH_H_
#define AUGUSTA_SERVER_FILE_WATCH_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

// Watching the Parameters script for changes (ADR-0039). The decision, whether
// what a poll saw is a change worth acting on, is ChangeDetector and needs no
// file, thread or clock, so it is tested with times of its own; FileWatcher is
// the mechanism that looks at the file on a thread and calls back.
//
// The file is polled and not subscribed to: one implementation serves the
// workstation and the cluster alike, and a mounted ConfigMap, which changes by
// swapping a link and so raises no event on the path itself, is seen because
// the signature holds where the path resolves to.
namespace augusta::server {

/// What one look at the watched file found. Two looks are the same file when
/// they are equal: the path resolves to the same place, last written at the
/// same time, and as long.
struct FileSignature {
  /// Where the path resolves to now, so a link swapped to another file differs
  /// even when the file it now reaches looks just like the old one.
  std::filesystem::path target;
  /// When the file it resolves to was last written.
  std::filesystem::file_time_type modified;
  /// How long that file is, in bytes.
  std::uintmax_t size = 0;

  bool operator==(const FileSignature&) const = default;
};

/// The signature of file as it is now, or nullopt if it can't be observed (it is missing, or unreadable).
std::optional<FileSignature> SignatureOf(const std::filesystem::path& file);

/// What to do about one poll.
enum class WatchEvent {
  /// Nothing to do.
  kNone,
  /// The file changed and has stopped changing: act on it.
  kChanged,
  /// The file can no longer be observed, and has not been for a while.
  kLost,
};

/// Turns polls into events. A change is reported once it has stopped changing
/// for the debounce time, so a burst of writes (an editor saving in steps)
/// gives one report, and a change undone before then gives none.
class ChangeDetector {
 public:
  /// Starts from baseline, what the file looked like when it was last acted on
  /// (nullopt if it could not be observed).
  ChangeDetector(std::optional<FileSignature> baseline, std::chrono::milliseconds debounce);

  /// Takes in what a poll at now saw.
  WatchEvent Feed(const std::optional<FileSignature>& seen, std::chrono::steady_clock::time_point now);

 private:
  std::chrono::milliseconds debounce_;
  // What the last poll saw, and since when it has looked so.
  std::optional<FileSignature> last_seen_;
  std::chrono::steady_clock::time_point seen_since_{};
  // What was last reported (or the baseline), so the same look is not reported twice.
  std::optional<FileSignature> acted_on_;
};

/// How often FileWatcher looks and how long the file must be still first.
struct WatchOptions {
  /// How long to wait between looks at the file.
  std::chrono::milliseconds poll_interval{250};
  /// How long the file must look the same before a change to it is acted on.
  std::chrono::milliseconds debounce{300};
};

/// Looks at file on a thread of its own and calls on_change on that thread each
/// time it has changed and settled. A file it cannot observe is logged and
/// nothing else: the watcher keeps looking, and calls back when the file
/// returns. The file as it is at construction is the baseline, not a change.
class FileWatcher {
 public:
  /// Starts watching file, and calls on_change from the watcher's thread; on_change must be safe to call from it.
  FileWatcher(std::filesystem::path file, const WatchOptions& options, std::function<void()> on_change);

  /// Stops the thread and waits for a callback in progress to end.
  ~FileWatcher();

  FileWatcher(const FileWatcher&) = delete;
  FileWatcher& operator=(const FileWatcher&) = delete;
  FileWatcher(FileWatcher&&) = delete;
  FileWatcher& operator=(FileWatcher&&) = delete;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_FILE_WATCH_H_
