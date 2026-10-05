#ifndef AUGUSTA_SERVER_CRASH_H_
#define AUGUSTA_SERVER_CRASH_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

/// \file
/// What augustad leaves behind when it crashes (ADR-0047): on a fatal signal it
/// writes the signal and a symbolized stack to stdout as CRITICAL log lines,
/// then dies of that signal, so the kernel still writes its core dump. main()
/// installs it once, before anything else runs. It writes past
/// augusta::logging, whose sink allocates and locks, so it formats its lines
/// itself, the way logging::FormatLine does. Where the core goes, and the
/// symbols that read it, are the node's and the build's (ADR-0047).
namespace augusta::server {

inline constexpr std::size_t kCrashLineCapacity = 512;

/// One crash log line, newline included; a longer one is cut to fit.
using CrashLine = std::array<char, kCrashLineCapacity>;

/// Writes into line `<time> CRITICAL subsystem=server event=crash signal=<name>`,
/// the signal's name (SIGSEGV) or, for one without, its number. time is from
/// the epoch to the year 9999. Async-signal-safe. Returns the line written.
std::string_view FormatCrashSignalLine(CrashLine& line, std::chrono::sys_seconds time, int signal);

/// Writes into line the frame at index of a crash's stack:
/// `... event=crash_frame index=<index> pc=0x<pc> symbol="<symbol>"`, without
/// symbol when it is nullptr (unresolved). A symbol too long for the line is
/// cut, still quoted. Async-signal-safe. Returns the line written.
std::string_view FormatCrashFrameLine(CrashLine& line, std::chrono::sys_seconds time, int index, std::uintptr_t pc,
                                      const char* symbol);

/// Makes a fatal signal (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) write its
/// crash lines to stdout, then end the process as it would have, core dump
/// included; on Linux it also raises the core size limit to the hard limit.
/// argv0 is main's argv[0], which the symbolizer needs. Call once, from main,
/// before other threads start.
void InstallCrashHandler(const char* argv0);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_CRASH_H_
