#include "run_files.h"

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

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "augusta/application.h"
#include "augusta/crash.h"

namespace augusta::client {

namespace {

// path in UTF-8: path::string() converts to the ANSI code page on Windows, and
// throws for a character it lacks.
std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return {text.begin(), text.end()};
}

constexpr std::string_view kLogsFolder = "logs";
constexpr std::string_view kDumpsFolder = "dumps";
constexpr std::string_view kPrefix = "augustac-";
constexpr std::string_view kLogExtension = ".log";
constexpr std::string_view kMinidumpExtension = ".dmp";
constexpr std::string_view kGpuDumpExtension = ".nv-gpudmp";
// How much of a log's end ReadRunEnd reads: a last line longer than this is
// not one it classifies, and no crash or terminal line is.
constexpr std::streamoff kTailBytes = 64 * 1024;

// How many characters a Run ID is in a file's name: its canonical form.
constexpr std::size_t kRunIdLength = 36;

// The Run ID text writes in the canonical form boost::uuids::to_string writes
// (lowercase, hyphenated, no braces), as LogPath and the dump paths name a
// file; anything else, upper case among it, names no Run.
std::optional<boost::uuids::uuid> ParseRunId(std::string_view text) {
  boost::uuids::uuid run;
  if (!boost::uuids::from_chars(text.data(), text.data() + text.size(), run) || boost::uuids::to_string(run) != text) {
    return std::nullopt;
  }
  return run;
}

// The Run ID name starts with after kPrefix, and what follows it, or nullopt.
struct NamedRun {
  boost::uuids::uuid run;
  std::string_view rest;
};

std::optional<NamedRun> SplitName(std::string_view name) {
  if (!name.starts_with(kPrefix) || name.size() < kPrefix.size() + kRunIdLength) {
    return std::nullopt;
  }
  const auto run = ParseRunId(name.substr(kPrefix.size(), kRunIdLength));
  if (!run) {
    return std::nullopt;
  }
  return NamedRun{.run = *run, .rest = name.substr(kPrefix.size() + kRunIdLength)};
}

// The Run a log's name names, if it is one LogPath makes.
std::optional<boost::uuids::uuid> RunOfLog(std::string_view name) {
  const auto named = SplitName(name);
  return named && named->rest == kLogExtension ? std::optional(named->run) : std::nullopt;
}

// The Run a dump's name names, if it is one MinidumpPath or GpuDumpPath makes.
std::optional<boost::uuids::uuid> RunOfDump(std::string_view name) {
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
std::map<boost::uuids::uuid, std::vector<std::filesystem::path>> ScanRuns(const std::filesystem::path& directory) {
  std::map<boost::uuids::uuid, std::vector<std::filesystem::path>> runs;
  const auto scan = [&runs](const std::filesystem::path& folder,
                            const std::function<std::optional<boost::uuids::uuid>(std::string_view)>& run_of) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
      std::error_code status_error;
      if (!it->is_regular_file(status_error)) {
        continue;
      }
      if (const auto run = run_of(Utf8(it->path().filename()))) {
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
// subsystem=<s> event=<e> ...` (ADR-0029), so its event is its fourth word,
// one crash.h's or application.h's when the Run crashed or failed.
RunEnd Classify(std::string_view line) {
  constexpr std::size_t kSubsystemWord = 2;
  constexpr std::size_t kEventWord = 3;
  constexpr std::string_view kEventKey = "event=";
  std::string_view event = Word(line, kEventWord);
  if (!Word(line, kSubsystemWord).starts_with("subsystem=") || !event.starts_with(kEventKey)) {
    return RunEnd::kOrdinary;
  }
  event.remove_prefix(kEventKey.size());
  if (event == crash::kCrashEvent || event == crash::kCrashFrameEvent) {
    return RunEnd::kCrash;
  }
  if (event == application::kTerminalFailureEvent) {
    return RunEnd::kTerminalFailure;
  }
  return RunEnd::kOrdinary;
}

}  // namespace

std::filesystem::path LogPath(const std::filesystem::path& directory, const boost::uuids::uuid& run) {
  return directory / kLogsFolder / std::format("{}{}{}", kPrefix, boost::uuids::to_string(run), kLogExtension);
}

std::filesystem::path MinidumpPath(const std::filesystem::path& directory, const boost::uuids::uuid& run) {
  return directory / kDumpsFolder / std::format("{}{}{}", kPrefix, boost::uuids::to_string(run), kMinidumpExtension);
}

std::filesystem::path GpuDumpPath(const std::filesystem::path& directory, const boost::uuids::uuid& run,
                                  std::uint32_t n) {
  return directory / kDumpsFolder /
         std::format("{}{}-{}{}", kPrefix, boost::uuids::to_string(run), n, kGpuDumpExtension);
}

std::vector<std::filesystem::path> RunFiles(const std::filesystem::path& directory, const boost::uuids::uuid& run) {
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
        undeleted.push_back({.path = file, .reason = error});
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

std::optional<PreviousRun> FindPreviousRun(const std::filesystem::path& directory, const boost::uuids::uuid& current) {
  auto runs = ScanRuns(directory);
  auto newer = runs.lower_bound(current);
  if (newer == runs.begin()) {
    return std::nullopt;
  }
  auto& [id, files] = *std::prev(newer);
  return PreviousRun{.id = id, .end = ReadRunEnd(LogPath(directory, id)), .files = std::move(files)};
}

}  // namespace augusta::client
