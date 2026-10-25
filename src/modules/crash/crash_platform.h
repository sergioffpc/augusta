#ifndef AUGUSTA_CRASH_CRASH_PLATFORM_H_
#define AUGUSTA_CRASH_CRASH_PLATFORM_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

#include "absl/functional/function_ref.h"
#include "augusta/crash.h"

/// \file
/// The handler's two halves, private to the module: crash.cpp formats a
/// crash's lines and keeps what Install was given, on every platform;
/// crash_windows.cpp or crash_posix.cpp, the one the platform builds, writes
/// the lines, walks the stack and catches the crash. Everything but
/// OpenLogFile and InstallHandlers runs inside a handler, so it allocates
/// nothing, takes no lock and calls only async-signal-safe functions.
namespace augusta::crash {

// The platform's half.

/// Opens path, the Run's log file, for appending beside the log sink, which
/// keeps it open too, so WriteLine writes to it; if it cannot be, the lines
/// reach stdout alone.
void OpenLogFile(const std::filesystem::path& path);

/// A crash line to stdout, where the log sink writes, and to the Run's log
/// file once OpenLogFile opened it.
void WriteLine(std::string_view line);

/// One line per frame of a crash's stack, innermost first, written through
/// line, each ending in context: its function on Linux and macOS
/// (FormatFrameLine), its module and offset on Windows (FormatModuleFrameLine).
void WriteFrameLines(std::span<void* const> frames, std::string_view subsystem, std::chrono::sys_seconds now,
                     std::string_view context, Line& line);

/// Catches the crashes the platform has: its fatal signals and, on Windows,
/// the unhandled exceptions, with settings, which outlive the handlers.
/// argv0 is main's.
void InstallHandlers(const char* argv0, const Settings& settings);

// crash.cpp's half, for the handlers.

/// Stores a crash's stack in frames, innermost first; returns how many.
using FrameCapture = absl::FunctionRef<int(std::span<void*>)>;

/// A fatal signal's lines: FormatSignalLine's, with the core's location, then
/// one per frame capture stores, each ending in the log's context.
void WriteSignalLines(int signal, FrameCapture capture);

/// An unhandled Windows exception's lines: FormatExceptionLine's, with its
/// code, then one per frame capture stores, each ending in the log's context.
void WriteExceptionLines(std::uint32_t code, FrameCapture capture);

}  // namespace augusta::crash

#endif  // AUGUSTA_CRASH_CRASH_PLATFORM_H_
