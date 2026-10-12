#ifndef AUGUSTA_SERVER_CAPTURE_RETENTION_H_
#define AUGUSTA_SERVER_CAPTURE_RETENTION_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "augusta/faults.h"

/// \file
/// How a server keeps its capture directory within a count and a size, oldest
/// Match first (ADR-0050, #461). Capturer's writer thread holds a
/// CaptureDirectory and asks it before creating each Match's file and before
/// writing each record; it deletes the oldest completed captures to make room,
/// and never runs on the Simulation thread (NFR-01). A completed capture is a
/// regular file of the directory itself, named as CaptureFileName names one and
/// starting with a capture's magic; oldest is by the Match start in its name.
/// Nothing else is ever touched. PlanRetention decides; CaptureDirectory scans
/// and deletes.
namespace augusta::server {

/// The limits of one server's capture directory, each off when nullopt.
struct CaptureRetention {
  /// How many captures it keeps, the one being written included; at least 1.
  std::optional<std::size_t> max_files;
  /// Their total size, the one being written included, in bytes.
  std::optional<std::uintmax_t> max_bytes;

  [[nodiscard]] bool Enabled() const { return max_files.has_value() || max_bytes.has_value(); }
};

/// What a capture directory's retention does, as it does it, for the server's
/// metrics: each called on the capture's writer thread, and an empty one calls
/// nothing. Must outlive the Capturer.
struct CaptureRetentionObserver {
  /// A completed capture was deleted.
  std::function<void()> on_deleted;
  /// A completed capture could not be deleted.
  std::function<void()> on_delete_failed;
  /// How many captures the directory holds and their total size in bytes, the
  /// one being written included, after a scan, a deletion or a write.
  std::function<void(std::size_t files, std::uintmax_t bytes)> on_directory;
};

/// Whether name is one CaptureFileName makes: YYYYMMDDTHHMMSSmmmZ-NNNN.capture,
/// the number of 4 digits or more.
[[nodiscard]] bool IsCaptureFileName(std::string_view name);

/// A completed capture in a capture directory.
struct CompletedCapture {
  std::filesystem::path path;
  /// In bytes.
  std::uintmax_t size = 0;
};

/// What to do before adding to a capture directory.
struct RetentionPlan {
  /// How many of the oldest completed captures to delete.
  std::size_t deletions = 0;
  /// Whether the addition then fits; false only when deleting every completed
  /// capture is not enough.
  bool fits = true;

  bool operator==(const RetentionPlan&) const = default;
};

/// Decision: how many of completed, oldest first, which with the capture being
/// written hold total bytes, retention has deleted before bytes more are
/// written, opening a new capture for them when opening.
[[nodiscard]] RetentionPlan PlanRetention(std::span<const CompletedCapture> completed, std::uintmax_t total,
                                          bool opening, std::uintmax_t bytes, const CaptureRetention& retention);

/// One server's capture directory as its writer thread keeps it within
/// retention: its completed captures, oldest first, and the bytes they and the
/// one being written hold, from one scan at each Match start plus what the
/// writer writes and minus what it deletes. A capture that cannot be deleted is
/// logged once per Match start and left out of both until the next, which
/// tries again; it never stops a capture. With retention off it still scans
/// and counts, for the metrics, but deletes nothing. Writer thread only.
class CaptureDirectory {
 public:
  /// faults, when given, is asked before each deletion
  /// (failure::Site::kCaptureDelete), a trip failing it as the disk would.
  CaptureDirectory(std::filesystem::path directory, CaptureRetention retention, CaptureRetentionObserver observer,
                   failure::Faults* faults);

  /// Before match's file, of the name file_name, is created to hold bytes
  /// first: rescans the directory, that file left out, and deletes the oldest
  /// completed captures until fewer than max_files remain and bytes fit.
  /// False when they do not even with none left, and nothing is to be written.
  [[nodiscard]] bool Open(std::uint64_t match, std::string_view file_name, std::uintmax_t bytes);

  /// Before bytes more are written to the capture open: deletes the oldest
  /// completed captures until they fit, and counts them as written. False when
  /// they do not even with none left, and nothing is to be written.
  [[nodiscard]] bool Reserve(std::uintmax_t bytes);

 private:
  // Mechanism: the directory's completed captures, oldest first, but file_name.
  [[nodiscard]] std::vector<CompletedCapture> Scan(std::string_view file_name) const;
  // The completed captures not yet deleted, oldest first.
  [[nodiscard]] std::span<const CompletedCapture> Remaining() const;
  // Deletes the oldest completed capture, or logs why it could not; either way
  // it is no longer one of completed_.
  void DeleteOldest();
  // Deletes until plan says bytes fit, or that they cannot.
  [[nodiscard]] bool MakeRoom(bool opening, std::uintmax_t bytes);
  void Report() const;

  const std::filesystem::path directory_;
  const CaptureRetention retention_;
  const CaptureRetentionObserver observer_;
  failure::Faults* const faults_;
  std::vector<CompletedCapture> completed_;
  // How many of completed_, from the oldest, are deleted.
  std::size_t deleted_ = 0;
  std::uintmax_t total_ = 0;
  // Whether the capture open counts as one of the directory's files.
  bool open_ = false;
  std::uint64_t match_ = 0;
  // Whether a failed deletion was logged since match_'s start.
  bool failure_logged_ = false;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_CAPTURE_RETENTION_H_
