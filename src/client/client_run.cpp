#include "augusta/client_run.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <boost/uuid/time_generator_v7.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "augusta/gpu_crash_dump_files.h"
#include "augusta/hardware.h"
#include "augusta/logging.h"
#include "augusta/run_files.h"
#include "augusta/version.h"

namespace augusta::client {

namespace {

// path in UTF-8 for a line: path::string() converts to the ANSI code page on
// Windows, and throws for a character it lacks.
std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return {text.begin(), text.end()};
}

// The generator every Run ID is taken from: one for the process, so a Run ID
// taken after another compares greater. BeginRun alone takes from it, on
// main's thread.
boost::uuids::time_generator_v7& GlobalRunIds() {
  static boost::uuids::time_generator_v7 generator;
  return generator;
}

// Adds the Run's log file as a second sink and returns it, or warns why it
// could not.
std::optional<std::filesystem::path> StartLogFile(const std::filesystem::path& directory,
                                                  const boost::uuids::uuid& run_id) {
  std::filesystem::path file = LogPath(directory, run_id);
  if (auto added = logging::AddFileSink(file); !added) {
    LOG_WARN("subsystem=client event=log_file_unavailable path=\"{}\" reason=\"{}\"", Utf8(file),
             logging::DescribeFileSinkError(added.error()));
    return std::nullopt;
  }
  return file;
}

// Deletes the files of the Runs before the last kKeptRuns, or warns of each it
// could not.
void PruneOldRuns(const std::filesystem::path& directory) {
  for (const UndeletedFile& file : PruneRuns(directory, kKeptRuns)) {
    LOG_WARN("subsystem=client event=run_file_undeleted path=\"{}\" reason=\"{}\"", Utf8(file.path),
             file.reason.message());
  }
}

}  // namespace

Run BeginRun(const std::optional<std::filesystem::path>& directory) {
  Run run{.id = GlobalRunIds()(), .log_file = std::nullopt, .minidump_file = std::nullopt};
  logging::SetContext(logging::ContextField::kRun, boost::uuids::to_string(run.id));
  if (directory) {
    run.log_file = StartLogFile(*directory, run.id);
    run.minidump_file = MinidumpPath(*directory, run.id);
    renderer::NameGpuCrashDumps(
        [directory = *directory, id = run.id](std::uint32_t n) { return GpuDumpPath(directory, id, n); });
  } else {
    LOG_WARN("subsystem=client event=log_file_unavailable reason=\"executable directory unknown\"");
  }
  LOG_INFO("subsystem=client event=starting version={} {}", EngineVersion(), DescribeBuild());
  LOG_INFO("subsystem=client event=hardware {}", hardware::DescribeMachine(hardware::QueryMachine()));
  return run;
}

void ReportAndPruneRuns(const std::optional<std::filesystem::path>& directory, const boost::uuids::uuid& current) {
  if (directory) {
    ReportPreviousRun(*directory, current);
    PruneOldRuns(*directory);
  }
}

void ReportPreviousRun(const std::filesystem::path& directory, const boost::uuids::uuid& current) {
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
  LOG_WARN("subsystem=client event=previous_run_crashed crashed_run={} directory=\"{}\" files=\"{}\"",
           boost::uuids::to_string(previous->id), Utf8(directory), files);
}

SessionLogContext::SessionLogContext(const boost::uuids::uuid& session) {
  logging::SetContext(logging::ContextField::kSession, boost::uuids::to_string(session));
}

SessionLogContext::~SessionLogContext() { logging::ClearContext(logging::ContextField::kSession); }

MatchLogContext::~MatchLogContext() { logging::ClearContext(logging::ContextField::kMatch); }

void MatchLogContext::Hold(const boost::uuids::uuid& match) {
  if (match.is_nil()) {
    logging::ClearContext(logging::ContextField::kMatch);
  } else {
    logging::SetContext(logging::ContextField::kMatch, boost::uuids::to_string(match));
  }
}

}  // namespace augusta::client
