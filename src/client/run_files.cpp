#include "augusta/run_files.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "augusta/uuid.h"

namespace augusta::client {

namespace {

constexpr std::string_view kLogsFolder = "logs";
constexpr std::string_view kDumpsFolder = "dumps";
constexpr std::string_view kPrefix = "augustac-";
constexpr std::string_view kLogExtension = ".log";
constexpr std::string_view kMinidumpExtension = ".dmp";
constexpr std::string_view kGpuDumpExtension = ".nv-gpudmp";
// The canonical form's length, which uuid::Parse alone accepts.
constexpr std::size_t kRunIdLength = 36;
// How much of a log's end ReadRunEnd reads: a last line longer than this is
// not one it classifies, and no crash or terminal line is.
constexpr std::streamoff kTailBytes = 64 * 1024;

// path's file name in UTF-8, which, unlike path.string() on Windows, never
// throws for a character the code page lacks.
std::string FileName(const std::filesystem::path& path) {
  const std::u8string utf8 = path.filename().u8string();
  return {utf8.begin(), utf8.end()};
}

// The Run ID name starts with after kPrefix, and what follows it, or nullopt.
struct NamedRun {
  uuid::Uuid run;
  std::string_view rest;
};

std::optional<NamedRun> SplitName(std::string_view name) {
  if (!name.starts_with(kPrefix) || name.size() < kPrefix.size() + kRunIdLength) {
    return std::nullopt;
  }
  const auto run = uuid::Parse(name.substr(kPrefix.size(), kRunIdLength));
  if (!run) {
    return std::nullopt;
  }
  return NamedRun{.run = *run, .rest = name.substr(kPrefix.size() + kRunIdLength)};
}

// The Run a log's name names, if it is one LogPath makes.
std::optional<uuid::Uuid> RunOfLog(std::string_view name) {
  const auto named = SplitName(name);
  return named && named->rest == kLogExtension ? std::optional(named->run) : std::nullopt;
}

// The Run a dump's name names, if it is one MinidumpPath or GpuDumpPath makes.
std::optional<uuid::Uuid> RunOfDump(std::string_view name) {
  const auto named = SplitName(name);
  if (!named) {
    return std::nullopt;
  }
  std::string_view rest = named->rest;
  if (rest == kMinidumpExtension) {
    return named->run;
  }
  if (!rest.starts_with('-') || !rest.ends_with(kGpuDumpExtension)) {
    return std::nullopt;
  }
  rest = rest.substr(1, rest.size() - 1 - kGpuDumpExtension.size());
  const bool numbered = !rest.empty() && std::ranges::all_of(rest, [](char c) { return c >= '0' && c <= '9'; });
  return numbered ? std::optional(named->run) : std::nullopt;
}

// Every Run's file in directory's two folders, by Run ID.
std::map<uuid::Uuid, std::vector<std::filesystem::path>> ScanRuns(const std::filesystem::path& directory) {
  std::map<uuid::Uuid, std::vector<std::filesystem::path>> runs;
  const auto scan = [&runs](const std::filesystem::path& folder,
                            const std::function<std::optional<uuid::Uuid>(std::string_view)>& run_of) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
      std::error_code status_error;
      if (!it->is_regular_file(status_error)) {
        continue;
      }
      if (const auto run = run_of(FileName(it->path()))) {
        runs[*run].push_back(it->path());
      }
    }
  };
  scan(directory / kLogsFolder, RunOfLog);
  scan(directory / kDumpsFolder, RunOfDump);
  return runs;
}

// The last line of text that is not blank, without its line ending.
std::string_view LastLine(std::string_view text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
    text.remove_suffix(1);
  }
  const std::size_t start = text.find_last_of('\n');
  return start == std::string_view::npos ? text : text.substr(start + 1);
}

// The index-th of line's words, split at spaces, or empty if it has fewer.
std::string_view Word(std::string_view line, std::size_t index) {
  for (; index > 0; --index) {
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos) {
      return {};
    }
    line.remove_prefix(space + 1);
  }
  return line.substr(0, line.find(' '));
}

// How a log whose last line is line ended: a line is `<time> <LEVEL>
// subsystem=<s> event=<e> ...` (ADR-0029), so its event is its fourth word.
RunEnd Classify(std::string_view line) {
  constexpr std::size_t kSubsystemWord = 2;
  constexpr std::size_t kEventWord = 3;
  if (!Word(line, kSubsystemWord).starts_with("subsystem=")) {
    return RunEnd::kOrdinary;
  }
  const std::string_view event = Word(line, kEventWord);
  if (event == "event=crash" || event == "event=crash_frame") {
    return RunEnd::kCrash;
  }
  if (event == "event=terminal_failure") {
    return RunEnd::kTerminalFailure;
  }
  return RunEnd::kOrdinary;
}

}  // namespace

std::filesystem::path LogPath(const std::filesystem::path& directory, const uuid::Uuid& run) {
  return directory / kLogsFolder / std::format("{}{}{}", kPrefix, run, kLogExtension);
}

std::filesystem::path MinidumpPath(const std::filesystem::path& directory, const uuid::Uuid& run) {
  return directory / kDumpsFolder / std::format("{}{}{}", kPrefix, run, kMinidumpExtension);
}

std::filesystem::path GpuDumpPath(const std::filesystem::path& directory, const uuid::Uuid& run, std::uint32_t n) {
  return directory / kDumpsFolder / std::format("{}{}-{}{}", kPrefix, run, n, kGpuDumpExtension);
}

std::vector<std::filesystem::path> RunFiles(const std::filesystem::path& directory, const uuid::Uuid& run) {
  auto runs = ScanRuns(directory);
  const auto found = runs.find(run);
  return found == runs.end() ? std::vector<std::filesystem::path>{} : std::move(found->second);
}

std::vector<UndeletedFile> PruneRuns(const std::filesystem::path& directory, std::size_t kept) {
  const auto runs = ScanRuns(directory);
  std::vector<UndeletedFile> undeleted;
  // The map holds the oldest Run first, so all but its last kept are pruned.
  const std::size_t pruned = runs.size() > kept ? runs.size() - kept : 0;
  auto run = runs.begin();
  for (std::size_t i = 0; i < pruned; ++i, ++run) {
    for (const auto& file : run->second) {
      std::error_code error;
      if (!std::filesystem::remove(file, error) && error) {
        undeleted.push_back({.path = file, .reason = error.message()});
      }
    }
  }
  return undeleted;
}

RunEnd ReadRunEnd(const std::filesystem::path& log) {
  std::ifstream file(log, std::ios::binary | std::ios::ate);
  if (!file) {
    return RunEnd::kOrdinary;
  }
  const std::streamoff size = file.tellg();
  const std::streamoff tail = std::min(size, kTailBytes);
  std::string text(static_cast<std::size_t>(tail), '\0');
  file.seekg(size - tail);
  file.read(text.data(), tail);
  text.resize(static_cast<std::size_t>(file.gcount()));
  return Classify(LastLine(text));
}

std::optional<PreviousRun> FindPreviousRun(const std::filesystem::path& directory, const uuid::Uuid& current) {
  auto runs = ScanRuns(directory);
  auto newer = runs.lower_bound(current);
  if (newer == runs.begin()) {
    return std::nullopt;
  }
  auto& [id, files] = *std::prev(newer);
  return PreviousRun{.id = id, .end = ReadRunEnd(LogPath(directory, id)), .files = std::move(files)};
}

}  // namespace augusta::client
