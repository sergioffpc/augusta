#include "augusta/client_run.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "augusta/config.h"
#include "augusta/hardware.h"
#include "augusta/logging.h"
#include "augusta/run_files.h"
#include "augusta/uuid.h"
#include "augusta/version.h"

namespace augusta::client {

namespace {

// path in UTF-8, which, unlike path.string() on Windows, never throws for a
// character the code page lacks.
std::string Utf8(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return {utf8.begin(), utf8.end()};
}

// Adds the Run's log file as a second sink and returns it, or warns why it
// could not.
std::optional<std::filesystem::path> StartLogFile(const std::filesystem::path& directory, const uuid::Uuid& run_id) {
  std::filesystem::path file = LogPath(directory, run_id);
  if (auto added = logging::AddFileSink(file); !added) {
    LOG_WARN("subsystem=client event=log_file_unavailable path=\"{}\" reason=\"{}\"", Utf8(file), added.error());
    return std::nullopt;
  }
  return file;
}

// Deletes the files of the Runs before the last kKeptRuns, or warns of each it
// could not.
void PruneOldRuns(const std::filesystem::path& directory) {
  for (const UndeletedFile& file : PruneRuns(directory, kKeptRuns)) {
    LOG_WARN("subsystem=client event=run_file_undeleted path=\"{}\" reason=\"{}\"", Utf8(file.path), file.reason);
  }
}

}  // namespace

Run BeginRun() {
  Run run{.id = uuid::Generate(), .log_file = std::nullopt, .minidump_file = std::nullopt};
  logging::SetContext(logging::ContextField::kRun, run.id.ToString());
  const auto directory = config::ExecutableDirectory();
  if (directory) {
    run.log_file = StartLogFile(*directory, run.id);
    run.minidump_file = MinidumpPath(*directory, run.id);
  } else {
    LOG_WARN("subsystem=client event=log_file_unavailable reason=\"executable directory unknown\"");
  }
  LOG_INFO("subsystem=client event=starting version={} {}", EngineVersion(), DescribeBuild());
  LOG_INFO("subsystem=client event=hardware {}", hardware::DescribeMachine(hardware::QueryMachine()));
  if (directory) {
    ReportPreviousRun(*directory, run.id);
    PruneOldRuns(*directory);
  }
  return run;
}

void ReportPreviousRun(const std::filesystem::path& directory, const uuid::Uuid& current) {
  const auto previous = FindPreviousRun(directory, current);
  if (!previous || previous->end != RunEnd::kCrash) {
    return;
  }
  // Each relative to directory, in the generic form: a Run's files' names hold
  // no space, so they can share one quoted value.
  std::vector<std::string> names;
  for (const auto& file : previous->files) {
    const std::u8string name = file.lexically_relative(directory).generic_u8string();
    names.emplace_back(name.begin(), name.end());
  }
  std::ranges::sort(names);
  std::string files;
  for (const auto& name : names) {
    files += (files.empty() ? "" : " ") + name;
  }
  LOG_WARN("subsystem=client event=previous_run_crashed crashed_run={} directory=\"{}\" files=\"{}\"", previous->id,
           Utf8(directory), files);
}

}  // namespace augusta::client
