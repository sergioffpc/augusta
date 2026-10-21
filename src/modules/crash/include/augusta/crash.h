#ifndef AUGUSTA_CRASH_H_
#define AUGUSTA_CRASH_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/// \file
/// What an executable leaves behind when it crashes (ADR-0047): on a fatal
/// signal it writes the signal and a symbolized stack to stdout as CRITICAL log
/// lines naming the executable's subsystem, then dies of that signal, so the
/// kernel still writes its core dump. Shared by client and server: each
/// executable's main() installs it once, before anything else runs. It writes
/// past augusta::logging, whose sink allocates and locks, so it formats its
/// lines itself, the way logging::FormatLine does. Where the core goes, and the
/// symbols that read it, are the node's and the build's (ADR-0047).
namespace augusta::crash {

inline constexpr std::size_t kLineCapacity = 512;

/// One crash log line, newline included; a longer one is cut to fit.
using Line = std::array<char, kLineCapacity>;

/// Writes into line `<time> CRITICAL subsystem=<subsystem> event=crash
/// signal=<name>`, the signal's name (SIGSEGV) or, for one without, its number,
/// then ` core="<core>"` and ` run=<run_id>` for each not empty. time is from
/// the epoch to the year 9999. Async-signal-safe. Returns the line written.
std::string_view FormatSignalLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int signal,
                                  std::string_view core = {}, std::string_view run_id = {});

/// Writes into line the frame at index of a crash's stack:
/// `... event=crash_frame index=<index> pc=0x<pc> symbol="<symbol>"`, without
/// symbol when it is nullptr (unresolved), then ` run=<run_id>` when it is not
/// empty. A symbol too long for the line is cut, still quoted. Async-signal-safe.
/// Returns the line written.
std::string_view FormatFrameLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int index,
                                 std::uintptr_t pc, const char* symbol, std::string_view run_id = {});

/// What an executable gives the handler where it has them; the server gives
/// none, and a crash then leaves only its lines on stdout and the core.
struct Settings {
  /// The directory the executable's Crash dumps go to.
  std::optional<std::filesystem::path> dumps_directory;
  /// The Run's ID, in its canonical form, which every crash line then ends in
  /// as `run=<id>`, where the log's context puts it (ADR-0029).
  std::optional<std::string> run_id;
  /// The Run's log file, which the crash lines then reach as well as stdout,
  /// appended to. Opened by Install, since the handler cannot open it.
  std::optional<std::filesystem::path> log_file;
  /// How to get the core out of where the kernel keeps it, named on the crash
  /// line as `core="<how>"`: `coredumpctl dump augustac` for a Linux client.
  std::optional<std::string> core_location;
};

/// Makes a fatal signal (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) write its
/// crash lines, naming subsystem, to stdout and to the settings' log file,
/// then end the process as it would have, core dump included; on Linux it also
/// raises the core size limit to the hard limit. A log file that cannot be
/// opened is left out, as the logging sink leaves it out. argv0 is main's
/// argv[0], which the symbolizer needs; subsystem must outlive the process, as
/// a literal does. Call once, from main, before other threads start.
void Install(const char* argv0, std::string_view subsystem, Settings settings = {});

}  // namespace augusta::crash

#endif  // AUGUSTA_CRASH_H_
