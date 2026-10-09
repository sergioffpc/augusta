#include "capture_retention.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "augusta/faults.h"
#include "augusta/logging.h"
#include "augusta/protocol.h"

namespace augusta::server {

namespace {

bool IsDigit(char character) { return character >= '0' && character <= '9'; }

// Whether the file at path starts with a capture's magic.
bool StartsWithMagic(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::array<char, protocol::kCaptureMagic.size()> magic{};
  in.read(magic.data(), magic.size());
  return static_cast<std::size_t>(in.gcount()) == magic.size() &&
         std::ranges::equal(magic, protocol::kCaptureMagic,
                            [](char read, std::byte expected) { return static_cast<std::byte>(read) == expected; });
}

}  // namespace

bool IsCaptureFileName(std::string_view name) {
  // D a digit: the Match start in UTC to the millisecond, then its number.
  constexpr std::string_view kStart = "DDDDDDDDTDDDDDDDDDZ-";
  constexpr std::string_view kExtension = ".capture";
  constexpr std::size_t kNumberDigits = 4;
  if (name.size() < kStart.size() + kNumberDigits + kExtension.size() || !name.ends_with(kExtension)) {
    return false;
  }
  const auto matches = [](char pattern, char character) {
    return pattern == 'D' ? IsDigit(character) : pattern == character;
  };
  const std::string_view number = name.substr(kStart.size(), name.size() - kStart.size() - kExtension.size());
  return std::ranges::equal(kStart, name.substr(0, kStart.size()), matches) && std::ranges::all_of(number, IsDigit);
}

RetentionPlan PlanRetention(std::span<const CompletedCapture> completed, std::uintmax_t total, bool opening,
                            std::uintmax_t bytes, const CaptureRetention& retention) {
  std::size_t deletions = 0;
  std::uintmax_t remaining = total;
  const auto delete_oldest = [&] {
    remaining -= std::min(remaining, completed[deletions].size);
    ++deletions;
  };
  if (opening && retention.max_files.has_value()) {
    // Fewer than max_files left, so that with the one opening there are at most max_files.
    while (completed.size() - deletions >= *retention.max_files) {
      delete_oldest();
    }
  }
  if (!retention.max_bytes.has_value()) {
    return RetentionPlan{.deletions = deletions, .fits = true};
  }
  while (remaining + bytes > *retention.max_bytes && deletions < completed.size()) {
    delete_oldest();
  }
  return RetentionPlan{.deletions = deletions, .fits = remaining + bytes <= *retention.max_bytes};
}

CaptureDirectory::CaptureDirectory(std::filesystem::path directory, CaptureRetention retention,
                                   CaptureRetentionObserver observer, failure::Faults* faults)
    : directory_(std::move(directory)),
      retention_(std::move(retention)),
      observer_(std::move(observer)),
      faults_(faults) {}

bool CaptureDirectory::Open(std::uint64_t match, std::string_view file_name, std::uintmax_t bytes) {
  match_ = match;
  failure_logged_ = false;
  open_ = false;
  completed_ = Scan(file_name);
  deleted_ = 0;
  total_ = 0;
  for (const CompletedCapture& capture : completed_) {
    total_ += capture.size;
  }
  if (!MakeRoom(true, bytes)) {
    Report();
    return false;
  }
  open_ = true;
  total_ += bytes;
  Report();
  return true;
}

bool CaptureDirectory::Reserve(std::uintmax_t bytes) {
  if (!MakeRoom(false, bytes)) {
    return false;
  }
  total_ += bytes;
  Report();
  return true;
}

std::vector<CompletedCapture> CaptureDirectory::Scan(std::string_view file_name) const {
  std::vector<CompletedCapture> found;
  std::error_code error;
  for (std::filesystem::directory_iterator entry(directory_, error), end; !error && entry != end;
       entry.increment(error)) {
    const std::string name = entry->path().filename().string();
    std::error_code status;
    // A symbolic link is not the directory's own file, even to a capture.
    if (name == file_name || !IsCaptureFileName(name) || !entry->is_regular_file(status) || entry->is_symlink(status)) {
      continue;
    }
    const std::uintmax_t size = entry->file_size(status);
    if (!status && StartsWithMagic(entry->path())) {
      found.push_back(CompletedCapture{.path = entry->path(), .size = size});
    }
  }
  // Their names start with their Match's start, so they sort oldest first.
  std::ranges::sort(found, {}, [](const CompletedCapture& capture) { return capture.path.filename(); });
  return found;
}

std::span<const CompletedCapture> CaptureDirectory::Remaining() const {
  return std::span(completed_).subspan(deleted_);
}

void CaptureDirectory::DeleteOldest() {
  const CompletedCapture& oldest = completed_[deleted_];
  ++deleted_;
  total_ -= std::min(total_, oldest.size);
  std::optional<std::string> failure = faults_ == nullptr ? std::nullopt : faults_->Trip(failure::Site::kCaptureDelete);
  if (!failure.has_value()) {
    std::error_code error;
    std::filesystem::remove(oldest.path, error);
    if (error) {
      failure = error.message();
    }
  }
  if (!failure.has_value()) {
    LI("subsystem=capture event=capture_deleted match={} path={}", match_, oldest.path.string());
    if (observer_.on_deleted) {
      observer_.on_deleted();
    }
    return;
  }
  if (observer_.on_delete_failed) {
    observer_.on_delete_failed();
  }
  if (!failure_logged_) {
    failure_logged_ = true;
    LW("subsystem=capture event=capture_retention_failed match={} path={} detail=\"{}\"", match_, oldest.path.string(),
       *failure);
  }
}

bool CaptureDirectory::MakeRoom(bool opening, std::uintmax_t bytes) {
  while (true) {
    const RetentionPlan plan = PlanRetention(Remaining(), total_, opening, bytes, retention_);
    if (plan.deletions == 0) {
      return plan.fits;
    }
    DeleteOldest();
  }
}

void CaptureDirectory::Report() const {
  if (observer_.on_directory) {
    observer_.on_directory(Remaining().size() + (open_ ? 1 : 0), total_);
  }
}

}  // namespace augusta::server
