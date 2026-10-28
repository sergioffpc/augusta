#ifndef AUGUSTA_CLIENT_RUN_H_
#define AUGUSTA_CLIENT_RUN_H_

#include <filesystem>
#include <optional>

#include <boost/uuid/uuid.hpp>

#include "augusta/application.h"
#include "augusta/crash.h"

/// \file
/// The start of one Run of augustac (GLOSSARY.md): its Run ID, and the files it
/// leaves beside the executable (ADR-0027), named as run_files.h names them;
/// and its Session's and its Match's IDs in its log while it holds them.
namespace augusta::client {

/// One Run, named by its ID, and the files it leaves.
struct Run {
  boost::uuids::uuid id;
  /// The file its log reaches, or nullopt if it reaches only the console.
  std::optional<std::filesystem::path> log_file;
  /// Where a crash on Windows writes its minidump, run_files.h's MinidumpPath.
  /// nullopt on Linux, whose core stays in systemd-coredump, or if the
  /// executable's directory is unknown.
  std::optional<std::filesystem::path> minidump_file;
  /// The executable's directory, which its files are under; nullopt if
  /// unknown.
  std::optional<std::filesystem::path> directory;
};

/// Takes the Run's ID, which every line from now on carries as `run=`
/// (ADR-0029), and names its files under directory, the executable's
/// (ADR-0027): its log `logs/augustac-<run-id>.log` and, on Windows, its
/// minidump. Opens and writes nothing, so the crash handler can be installed
/// with the Run (CrashSettings) before anything else is done. Call once, after
/// logging::Init and before the config is read, so a config failure is named
/// by the Run too.
Run TakeRun(const std::optional<std::filesystem::path>& directory);

/// Starts run's log: every line from now on also reaches its log file, or, if
/// run's directory is unknown or that file cannot be opened, only the console,
/// with a warning saying why; then the returned Run has no log file. A GPU
/// crash's Aftermath dumps go to the Run's
/// `dumps/augustac-<run-id>-<n>.nv-gpudmp` (gpu_crash_dump_files.h). Then
/// writes the Run's first lines: the version and build (augusta/version.h),
/// the machine and the allocator, mimalloc as main() read it or the platform's
/// when it read none (a sanitizer build, ADR-0078), and each of its GPUs and
/// their drivers (augusta/hardware.h). The Renderer later names the GPU it
/// chooses. Call once, after TakeRun and the crash handler's install.
Run BeginRun(Run run, const std::optional<application::MimallocFacts>& mimalloc);

/// What the crash handler is given of run: its lines carry the Run ID and reach
/// the Run's log too. On Windows a crash writes the Run's minidump; on Linux,
/// where the kernel's core of the crash stays in the player's systemd-coredump,
/// the crash line says how to get it out (ADR-0047, ADR-0055).
crash::Settings CrashSettings(const Run& run);

/// Writes one WARN line, `event=previous_run_crashed`, naming the Run before
/// current in directory and its files when that Run ended in a crash, so a
/// player learns of it though its window closed; nothing otherwise. Call once
/// the crash handler is installed, so a crash here is reported too.
void ReportPreviousRun(const std::filesystem::path& directory, const boost::uuids::uuid& current);

/// Deletes the files in directory of all but the last run_files.h kKeptRuns
/// Runs, the current one included, warning of any it could not. Call after
/// ReportPreviousRun, which reads the Run before the current one.
void PruneOldRuns(const std::filesystem::path& directory);

/// While it lives, every line carries `session=<session>` (ADR-0029): held by
/// ClientRuntime from the admission it sees to the Session's end, so the
/// client's lines map to the server's about the same Session.
class SessionLogContext {
 public:
  explicit SessionLogContext(const boost::uuids::uuid& session);
  ~SessionLogContext();

  SessionLogContext(const SessionLogContext&) = delete;
  SessionLogContext& operator=(const SessionLogContext&) = delete;
  SessionLogContext(SessionLogContext&&) = delete;
  SessionLogContext& operator=(SessionLogContext&&) = delete;
};

/// Every line carries `match=<id>` from a Match start it is told of to that
/// Match's end, and none once it goes (ADR-0029): ClientRuntime hands its Hold
/// to its Session as harness::SessionConfig::on_match, so the client's lines
/// map to the server's and the capture's about the same Match. The client alone
/// holds one, as it holds one Session at a time.
class MatchLogContext {
 public:
  MatchLogContext() = default;
  ~MatchLogContext();

  MatchLogContext(const MatchLogContext&) = delete;
  MatchLogContext& operator=(const MatchLogContext&) = delete;
  MatchLogContext(MatchLogContext&&) = delete;
  MatchLogContext& operator=(MatchLogContext&&) = delete;

  /// match on every line from now on; with the nil ID, which a Match end and
  /// a Match start that names no Match tell, none.
  void Hold(const boost::uuids::uuid& match);
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_RUN_H_
