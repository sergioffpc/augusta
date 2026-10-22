#ifndef AUGUSTA_CLIENT_RUN_H_
#define AUGUSTA_CLIENT_RUN_H_

#include <filesystem>
#include <optional>

#include <boost/uuid/uuid.hpp>

/// \file
/// The start of one Run of augustac (GLOSSARY.md): its Run ID, and the files it
/// leaves beside the executable (ADR-0027), named as run_files.h names them;
/// and its Session's and its Match's IDs in its log while it holds them.
namespace augusta::client {

/// A Run once it has started.
struct Run {
  boost::uuids::uuid id;
  /// The file its log reaches, or nullopt if it reaches only the console.
  std::optional<std::filesystem::path> log_file;
  /// Where a crash on Windows writes its minidump, run_files.h's MinidumpPath,
  /// or nullopt if the executable's directory is unknown.
  std::optional<std::filesystem::path> minidump_file;
};

/// Takes the Run's ID and starts its log: every line from now on carries `run=`
/// (ADR-0029) and also reaches `logs/augustac-<run-id>.log` under directory,
/// the executable's (ADR-0027), or, if directory is unknown or that file cannot
/// be opened, only the console, with a warning saying why. A GPU crash's
/// Aftermath dumps go to the Run's `dumps/augustac-<run-id>-<n>.nv-gpudmp`
/// there (gpu_crash_dump_files.h). Then writes the Run's first lines: the
/// version and build (augusta/version.h), then the machine
/// (augusta/hardware.h); the GPU and its driver follow from the Renderer once
/// it chooses one. Call once, after logging::Init and before the config is
/// read, so a config failure is named by the Run too.
Run BeginRun(const std::optional<std::filesystem::path>& directory);

/// What a launch does with the Runs before current in directory: reports the
/// previous one if it crashed (ReportPreviousRun), then deletes the files of
/// all but the last run_files.h kKeptRuns Runs, current included, warning of
/// any it could not. Nothing if directory is unknown. Call once the crash
/// handler is installed, so a crash here is reported too.
void ReportAndPruneRuns(const std::optional<std::filesystem::path>& directory, const boost::uuids::uuid& current);

/// Writes one WARN line, `event=previous_run_crashed`, naming the Run before
/// current in directory and its files when that Run ended in a crash, so a
/// player learns of it though its window closed; nothing otherwise.
void ReportPreviousRun(const std::filesystem::path& directory, const boost::uuids::uuid& current);

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
/// Match's end, and none once it goes (ADR-0029): ClientRuntime hands Hold to
/// its Session as harness::SessionConfig::on_match, so the client's lines map
/// to the server's and the capture's about the same Match. The client alone
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
  static void Hold(const boost::uuids::uuid& match);
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_RUN_H_
