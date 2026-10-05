#include "crash.h"

#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <limits>
#include <span>
#include <string_view>

#include "absl/debugging/symbolize.h"

#ifdef _WIN32
#include <windows.h>

#include "absl/debugging/stacktrace.h"
#else
#include <cerrno>

#include <execinfo.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace augusta::server {

namespace {

// Everything from here to the handler runs inside it, so it allocates nothing,
// takes no lock and calls only async-signal-safe functions.

#ifdef _WIN32
constexpr std::array kFatalSignals{SIGSEGV, SIGILL, SIGFPE, SIGABRT};
#else
constexpr std::array kFatalSignals{SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
#endif

constexpr int kMaxFrames = 64;
// Room for a demangled name; CrashLine cuts a longer one anyway.
constexpr std::size_t kSymbolCapacity = sizeof(CrashLine);

constexpr std::int64_t kSecondsPerMinute = 60;
constexpr std::int64_t kSecondsPerHour = 60 * kSecondsPerMinute;
constexpr std::int64_t kSecondsPerDay = 24 * kSecondsPerHour;

constexpr std::uint64_t kDecimal = 10;
constexpr std::uint64_t kHexadecimal = 16;

// Appends to a CrashLine, always keeping room for the newline Finish ends it
// with, so a line too long is cut rather than overrun.
class LineWriter {
 public:
  explicit LineWriter(CrashLine& line) : line_(line) {}

  // Whether n characters still fit with reserve kept after them.
  [[nodiscard]] bool Fits(std::size_t n, std::size_t reserve) const { return size_ + n + reserve <= line_.size(); }

  void Put(char c, std::size_t reserve = 1) {
    if (Fits(1, reserve)) {
      line_[size_++] = c;
    }
  }

  void Put(std::string_view text) {
    for (const char c : text) {
      Put(c);
    }
  }

  // value in base (up to 16), zero-padded to width digits.
  void PutDigits(std::uint64_t value, std::uint64_t base, int width = 1) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    // Enough for any std::uint64_t in base 2 or more.
    std::array<char, std::numeric_limits<std::uint64_t>::digits> digits{};
    int count = 0;
    do {
      digits[count++] = kDigits[value % base];
      value /= base;
    } while (value != 0 || count < width);
    while (count > 0) {
      Put(digits[--count]);
    }
  }

  // text in double quotes, its quotes and backslashes escaped. Cut short if the
  // line runs out, but always closed.
  void PutQuoted(std::string_view text) {
    // The closing quote and the newline.
    constexpr std::size_t kClosing = 2;
    Put('"');
    for (const char c : text) {
      const bool escaped = c == '"' || c == '\\';
      if (!Fits(escaped ? 2 : 1, kClosing)) {
        break;
      }
      if (escaped) {
        Put('\\', kClosing);
      }
      Put(c, kClosing);
    }
    Put('"');
  }

  std::string_view Finish() {
    line_[size_++] = '\n';
    return {line_.data(), size_};
  }

 private:
  CrashLine& line_;
  std::size_t size_ = 0;
};

// `YYYY-MM-DDTHH:MM:SSZ`, as logging::FormatLine writes it, from time
// through the proleptic Gregorian calendar (H. Hinnant's civil_from_days):
// std::chrono's calendar types would do it too, but std::format allocates.
void PutTimestamp(LineWriter& writer, std::chrono::sys_seconds time) {
  const std::int64_t unix_seconds = time.time_since_epoch().count();
  const std::int64_t days = unix_seconds / kSecondsPerDay;
  const std::int64_t second_of_day = unix_seconds % kSecondsPerDay;
  const std::int64_t shifted = days + 719468;  // Days from 0000-03-01.
  const std::int64_t era = shifted / 146097;
  const std::int64_t day_of_era = shifted - (era * 146097);
  const std::int64_t year_of_era =
      (day_of_era - (day_of_era / 1460) + (day_of_era / 36524) - (day_of_era / 146096)) / 365;
  const std::int64_t day_of_year = day_of_era - ((365 * year_of_era) + (year_of_era / 4) - (year_of_era / 100));
  const std::int64_t shifted_month = ((5 * day_of_year) + 2) / 153;  // March is 0.
  const std::int64_t day = day_of_year - (((153 * shifted_month) + 2) / 5) + 1;
  const std::int64_t month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
  const std::int64_t year = year_of_era + (era * 400) + (month <= 2 ? 1 : 0);

  writer.PutDigits(static_cast<std::uint64_t>(year), kDecimal, 4);
  writer.Put('-');
  writer.PutDigits(static_cast<std::uint64_t>(month), kDecimal, 2);
  writer.Put('-');
  writer.PutDigits(static_cast<std::uint64_t>(day), kDecimal, 2);
  writer.Put('T');
  writer.PutDigits(static_cast<std::uint64_t>(second_of_day / kSecondsPerHour), kDecimal, 2);
  writer.Put(':');
  writer.PutDigits(static_cast<std::uint64_t>((second_of_day % kSecondsPerHour) / kSecondsPerMinute), kDecimal, 2);
  writer.Put(':');
  writer.PutDigits(static_cast<std::uint64_t>(second_of_day % kSecondsPerMinute), kDecimal, 2);
  writer.Put('Z');
}

void PutCrashPrefix(LineWriter& writer, std::chrono::sys_seconds time) {
  PutTimestamp(writer, time);
  writer.Put(" CRITICAL subsystem=server ");
}

std::string_view SignalName(int signal) {
  switch (signal) {
    case SIGSEGV:
      return "SIGSEGV";
    case SIGILL:
      return "SIGILL";
    case SIGFPE:
      return "SIGFPE";
    case SIGABRT:
      return "SIGABRT";
#ifdef SIGBUS
    case SIGBUS:
      return "SIGBUS";
#endif
    default:
      return {};
  }
}

void WriteToStdout(std::string_view text) {
#ifdef _WIN32
  DWORD written = 0;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
#else
  while (!text.empty()) {
    const ssize_t written = write(STDOUT_FILENO, text.data(), text.size());
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      return;
    }
    text.remove_prefix(static_cast<std::size_t>(written));
  }
#endif
}

// The crashing thread's return addresses, innermost first, this handler's own
// frames included. On Linux glibc's backtrace unwinds by the binary's unwind
// tables, so frames compiled without frame pointers are not lost; it is not
// listed as async-signal-safe only because its first call loads libgcc_s,
// which InstallCrashHandler does ahead of any crash.
int CaptureFrames(std::span<void*> frames) {
#ifdef _WIN32
  return absl::GetStackTrace(frames.data(), static_cast<int>(frames.size()), 0);
#else
  return backtrace(frames.data(), static_cast<int>(frames.size()));
#endif
}

void ReportCrash(int signal) {
  // time() rather than system_clock::now(): POSIX lists it as async-signal-safe.
  const std::chrono::sys_seconds now{std::chrono::seconds{std::time(nullptr)}};
  CrashLine line{};
  WriteToStdout(FormatCrashSignalLine(line, now, signal));

  std::array<void*, kMaxFrames> frames{};
  const int depth = CaptureFrames(frames);
  std::array<char, kSymbolCapacity> symbol{};
  for (int index = 0; index < depth; ++index) {
    void* const pc = frames[static_cast<std::size_t>(index)];
    const bool resolved = absl::Symbolize(pc, symbol.data(), static_cast<int>(symbol.size()));
    WriteToStdout(FormatCrashFrameLine(line, now, index, reinterpret_cast<std::uintptr_t>(pc),
                                       resolved ? symbol.data() : nullptr));
  }
}

extern "C" void HandleFatalSignal(int signal) {
  // Back to the default first, so a second fault - in another thread, or in
  // this report - ends the process at once instead of reporting again.
  for (const int fatal : kFatalSignals) {
    std::signal(fatal, SIG_DFL);
  }
  ReportCrash(signal);
  // Pending until the handler returns, then delivered to the default action:
  // the process dies of it and the kernel dumps its core. Not as PID 1, whose
  // own signals the kernel drops, hence the image's init (ADR-0047).
  std::raise(signal);
}

#ifndef _WIN32
// A core dump is written only under a non-zero RLIMIT_CORE, whose soft limit a
// shell or container runtime may leave at 0; the hard limit is as far as an
// unprivileged process may raise it.
void RaiseCoreLimit() {
  rlimit limit{};
  if (getrlimit(RLIMIT_CORE, &limit) == 0) {
    limit.rlim_cur = limit.rlim_max;
    setrlimit(RLIMIT_CORE, &limit);
  }
}
#endif

}  // namespace

std::string_view FormatCrashSignalLine(CrashLine& line, std::chrono::sys_seconds time, int signal) {
  LineWriter writer(line);
  PutCrashPrefix(writer, time);
  writer.Put("event=crash signal=");
  if (const std::string_view name = SignalName(signal); !name.empty()) {
    writer.Put(name);
  } else {
    writer.PutDigits(static_cast<std::uint64_t>(signal), kDecimal);
  }
  return writer.Finish();
}

std::string_view FormatCrashFrameLine(CrashLine& line, std::chrono::sys_seconds time, int index, std::uintptr_t pc,
                                      const char* symbol) {
  LineWriter writer(line);
  PutCrashPrefix(writer, time);
  writer.Put("event=crash_frame index=");
  writer.PutDigits(static_cast<std::uint64_t>(index), kDecimal);
  writer.Put(" pc=0x");
  writer.PutDigits(pc, kHexadecimal);
  if (symbol != nullptr) {
    writer.Put(" symbol=");
    writer.PutQuoted(symbol);
  }
  return writer.Finish();
}

void InstallCrashHandler(const char* argv0) {
  absl::InitializeSymbolizer(argv0);
  // Its first call loads what it needs (see CaptureFrames).
  std::array<void*, 1> warm_up{};
  CaptureFrames(warm_up);
#ifndef _WIN32
  RaiseCoreLimit();
#endif
  for (const int signal : kFatalSignals) {
    std::signal(signal, HandleFatalSignal);
  }
}

}  // namespace augusta::server
