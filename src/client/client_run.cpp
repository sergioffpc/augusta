#include "augusta/client_run.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <boost/uuid/time_generator_v7.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "augusta/crash.h"
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
// taken after another compares greater. TakeRun alone takes from it, on main's
// thread.
boost::uuids::time_generator_v7& GlobalRunIds() {
  static boost::uuids::time_generator_v7 generator;
  return generator;
}

// Adds file as the log's second sink and returns it, or warns why it could not.
std::optional<std::filesystem::path> StartLogFile(std::filesystem::path file) {
  if (auto added = logging::AddFileSink(file); !added) {
    LOG_WARN("subsystem=client event=log_file_unavailable path=\"{}\" reason=\"{}\"", Utf8(file),
             logging::DescribeFileSinkError(added.error()));
    return std::nullopt;
  }
  return file;
}

}  // namespace

Run TakeRun(const std::optional<std::filesystem::path>& directory) {
  Run run{.id = GlobalRunIds()(), .log_file = std::nullopt, .minidump_file = std::nullopt, .directory = directory};
  logging::SetContext(logging::ContextField::kRun, boost::uuids::to_string(run.id));
  if (directory) {
    run.log_file = LogPath(*directory, run.id);
#ifdef _WIN32
    // Only Windows writes a minidump; Linux's core stays in systemd-coredump.
    run.minidump_file = MinidumpPath(*directory, run.id);
#endif
  }
  return run;
}

Run BeginRun(Run run) {
  if (run.log_file) {
    run.log_file = StartLogFile(*std::move(run.log_file));
  } else {
    LOG_WARN("subsystem=client event=log_file_unavailable reason=\"executable directory unknown\"");
  }
  if (run.directory) {
    renderer::NameGpuCrashDumps(
        [directory = *run.directory, id = run.id](std::uint32_t n) { return GpuDumpPath(directory, id, n); });
  }
  LOG_INFO("subsystem=client event=starting version={} {}", EngineVersion(), DescribeBuild());
  LOG_INFO("subsystem=client event=hardware {}", hardware::DescribeMachine(hardware::QueryMachine()));
  const std::vector<hardware::Gpu> gpus = hardware::QueryGpus();
  if (gpus.empty()) {
    LOG_WARN("subsystem=client event=gpu_unknown");
  }
  for (std::size_t index = 0; index < gpus.size(); ++index) {
    LOG_INFO("subsystem=client event=gpu index={} {}", index, hardware::DescribeGpu(gpus[index]));
  }
  return run;
}

crash::Settings CrashSettings(const Run& run) {
  // Only a Linux crash leaves a core; only a Windows Run names a minidump
  // (TakeRun).
#ifdef _WIN32
  std::optional<std::string> core_location;
#else
  std::optional<std::string> core_location = "coredumpctl dump augustac";
#endif
  return {.minidump_file = run.minidump_file,
          .run_id = run.id,
          .log_file = run.log_file,
          .core_location = std::move(core_location)};
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

void PruneOldRuns(const std::filesystem::path& directory) {
  for (const UndeletedFile& file : PruneRuns(directory, kKeptRuns)) {
    LOG_WARN("subsystem=client event=run_file_undeleted path=\"{}\" reason=\"{}\"", Utf8(file.path),
             file.reason.message());
  }
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
