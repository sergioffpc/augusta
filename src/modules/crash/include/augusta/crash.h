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
/// symbols that read it, are the node's and the build's (ADR-0047). On Windows
/// a crash is an unhandled exception, whose stack it writes as modules and
/// offsets, then a minidump of itself (ADR-0055).
namespace augusta::crash {

inline constexpr std::size_t kLineCapacity = 512;

/// One crash log line, newline included; a longer one is cut to fit.
using Line = std::array<char, kLineCapacity>;

// Each line below ends in context, the log's context fields as every other
// line ends in them (logging::CopyContext): ` run=<id> session=<id>
// match=<id>`, those that are set, or nothing.

/// Writes into line `<time> CRITICAL subsystem=<subsystem> event=crash
/// signal=<name>`, the signal's name (SIGSEGV) or, for one without, its number,
/// then ` core="<core>"` when it is not empty, then context. time is from the
/// epoch to the year 9999. Async-signal-safe. Returns the line written.
std::string_view FormatSignalLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int signal,
                                  std::string_view core = {}, std::string_view context = {});

/// Writes into line the frame at index of a crash's stack:
/// `... event=crash_frame index=<index> pc=0x<pc> symbol="<symbol>"`, without
/// symbol when it is nullptr (unresolved), then context. A symbol too long for
/// the line is cut, still quoted, the context kept whole. Async-signal-safe.
/// Returns the line written.
std::string_view FormatFrameLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int index,
                                 std::uintptr_t pc, const char* symbol, std::string_view context = {});

/// Writes into line the frame at index of a Windows crash's stack:
/// `... event=crash_frame index=<index> pc=0x<pc> module="<module>"
/// offset=0x<offset>`, the file name of the module pc is in and pc's offset
/// into it, without either when module is nullptr (no module holds pc), then
/// context. The release's PDB symbolizes it later (ADR-0055). A module too long
/// for the line is cut, still quoted, its offset and the context kept whole.
/// Async-signal-safe. Returns the line written.
std::string_view FormatModuleFrameLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int index,
                                       std::uintptr_t pc, const char* module, std::uintptr_t offset,
                                       std::string_view context = {});

/// Writes into line `<time> CRITICAL subsystem=<subsystem> event=crash
/// exception=0x<code>`, the unhandled Windows exception's code in lowercase
/// hexadecimal, eight digits (0xc0000005 for an access violation), then
/// context. Returns the line written.
std::string_view FormatExceptionLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time,
                                     std::uint32_t code, std::string_view context = {});

/// The type of the minidump's user stream that holds the Run ID, in its
/// canonical form with no terminator: above the stream types dbghelp reserves
/// (up to LastReservedStream, 0xffff), and `AUGR` in ASCII.
inline constexpr std::uint32_t kRunIdStreamType = 0x41554752;

/// What an executable gives the handler where it has them; the server gives
/// none, and a crash then leaves only its lines on stdout and the core.
struct Settings {
  /// The minidump a crash writes on Windows, its directory made if missing:
  /// the client's `dumps/augustac-<run-id>.dmp` (ADR-0055). None on Linux,
  /// where the kernel keeps the core.
  std::optional<std::filesystem::path> minidump_file;
  /// The Run's ID, in its canonical form, which the Windows minidump holds in
  /// its kRunIdStreamType user stream. The lines' `run=` is the log's context's
  /// (logging::SetContext), as on every other line.
  std::optional<std::string> run_id;
  /// The Run's log file, which the crash lines then reach as well as stdout,
  /// appended to. Opened by Install, since the handler cannot open it.
  std::optional<std::filesystem::path> log_file;
  /// How to get the core out of where the kernel keeps it, named on the crash
  /// line as `core="<how>"`: `coredumpctl dump augustac` for a Linux client.
  std::optional<std::string> core_location;
};

/// Makes a fatal signal (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) write its
/// crash lines, naming subsystem and ending in the log's context as it is at
/// the crash (logging::CopyContext), to stdout and to the settings' log file,
/// then end the process as it would have, core dump included; on Linux it also
/// raises the core size limit to the hard limit. On Windows a crash is an
/// exception no handler took, in any thread, which an unhandled-exception
/// filter meets in place of SIGSEGV, SIGILL and SIGFPE: it writes the
/// exception's line and one line per frame (FormatModuleFrameLine), then the
/// settings' minidump, the Run ID in its kRunIdStreamType user stream, and ends
/// the process with the exception's code; SIGABRT writes the same lines and
/// minidump. A corrupted process may fail to write it. A log file that cannot
/// be opened is left out, as the logging sink leaves it out. argv0 is main's
/// argv[0], which the Linux symbolizer needs; subsystem must outlive the
/// process, as a literal does. Call once, from main, before other threads
/// start.
void Install(const char* argv0, std::string_view subsystem, Settings settings = {});

}  // namespace augusta::crash

#endif  // AUGUSTA_CRASH_H_
