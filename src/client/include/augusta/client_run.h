#ifndef AUGUSTA_CLIENT_RUN_H_
#define AUGUSTA_CLIENT_RUN_H_

#include <filesystem>
#include <optional>

#include "augusta/uuid.h"

/// \file
/// The start of one Run of augustac (GLOSSARY.md): its Run ID, and the files it
/// leaves beside the executable (ADR-0027), named as run_files.h names them.
namespace augusta::client {

/// A Run once it has started.
struct Run {
  uuid::Uuid id;
  /// The file its log reaches, or nullopt if it reaches only the console.
  std::optional<std::filesystem::path> log_file;
};

/// Takes the Run's ID and starts its log: every line from now on carries `run=`
/// (ADR-0029) and also reaches `logs/augustac-<run-id>.log` beside the
/// executable, or, if that file cannot be opened, only the console, with a
/// warning saying why. A GPU crash's Aftermath dumps go to the Run's
/// `dumps/augustac-<run-id>-<n>.nv-gpudmp` (gpu_crash_dump_files.h). Then
/// writes the Run's first lines: the version and build (augusta/version.h),
/// then the machine (augusta/hardware.h); the GPU and its driver follow from
/// the Renderer once it chooses one. Then reports
/// the previous Run if it crashed (ReportPreviousRun) and deletes the files of
/// all but the last run_files.h kKeptRuns Runs, this one included, warning of
/// any it could not. Call once, after logging::Init and before the config is
/// read, so a config failure is named by the Run too.
Run BeginRun();

/// Writes one WARN line, `event=previous_run_crashed`, naming the Run before
/// current in directory and its files when that Run ended in a crash, so a
/// player learns of it though its window closed; nothing otherwise.
void ReportPreviousRun(const std::filesystem::path& directory, const uuid::Uuid& current);

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_RUN_H_
