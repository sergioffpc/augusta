#ifndef AUGUSTA_RUN_FILES_H_
#define AUGUSTA_RUN_FILES_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <system_error>
#include <vector>

#include <boost/uuid/uuid.hpp>

/// \file
/// The files a Run of augustac leaves on disk (GLOSSARY.md), under the
/// directory it is given, the executable's: its log in `logs/` (ADR-0027) and
/// its Crash dumps in `dumps/` (ADR-0055), each named by its Run ID. Decisions
/// over that directory alone: the names, which Runs are kept, and how the
/// previous Run ended. No process state; client_run.h runs them at startup.
/// A file whose name is not one of these, in the folder it belongs to, is never
/// touched.
namespace augusta::client {

/// How many Runs' files the client keeps, its current Run's included.
inline constexpr std::size_t kKeptRuns = 10;

/// `<directory>/logs/augustac-<run-id>.log`: the Run's log.
[[nodiscard]] std::filesystem::path LogPath(const std::filesystem::path& directory, const boost::uuids::uuid& run);

/// `<directory>/dumps/augustac-<run-id>.dmp`: the minidump a crash on Windows writes.
[[nodiscard]] std::filesystem::path MinidumpPath(const std::filesystem::path& directory, const boost::uuids::uuid& run);

/// `<directory>/dumps/augustac-<run-id>-<n>.nv-gpudmp`: the Run's n-th
/// Aftermath dump of a GPU crash, from 1.
[[nodiscard]] std::filesystem::path GpuDumpPath(const std::filesystem::path& directory, const boost::uuids::uuid& run,
                                                std::uint32_t n);

/// The files of run directory holds, in either folder, in no order.
[[nodiscard]] std::vector<std::filesystem::path> RunFiles(const std::filesystem::path& directory,
                                                          const boost::uuids::uuid& run);

/// A Run's file pruning could not delete, and why.
struct UndeletedFile {
  std::filesystem::path path;
  /// The system's reason.
  std::error_code reason;
};

/// Deletes the files of every Run but the kept newest, by Run ID, across
/// `logs/` and `dumps/` together: a Run counts once whichever of its files are
/// left. Returns the files it could not delete; a missing directory has none.
std::vector<UndeletedFile> PruneRuns(const std::filesystem::path& directory, std::size_t kept);

/// How a Run ended, as the last line of its log says.
enum class RunEnd : std::uint8_t {
  /// Any other last line: it stopped as asked, or it was killed, or it has no
  /// log.
  kOrdinary,
  /// The crash handler's: `event=crash` or `event=crash_frame` (ADR-0047).
  kCrash,
  /// The application boundary's terminal event, `event=terminal_failure` (ADR-0033).
  kTerminalFailure,
};

/// How the Run whose log is log ended, from its last line that is not blank;
/// an empty or missing log is an ordinary end.
[[nodiscard]] RunEnd ReadRunEnd(const std::filesystem::path& log);

/// The Run before this one, and how it ended.
struct PreviousRun {
  boost::uuids::uuid id;
  RunEnd end = RunEnd::kOrdinary;
  /// Its files, as RunFiles gives them.
  std::vector<std::filesystem::path> files;
};

/// The newest Run in directory older than current, or nullopt if there is none.
[[nodiscard]] std::optional<PreviousRun> FindPreviousRun(const std::filesystem::path& directory,
                                                         const boost::uuids::uuid& current);

}  // namespace augusta::client

#endif  // AUGUSTA_RUN_FILES_H_
