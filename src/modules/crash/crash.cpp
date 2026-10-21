#include "augusta/crash.h"

#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "absl/debugging/symbolize.h"

#ifdef _WIN32
#include <windows.h>

#include "absl/debugging/stacktrace.h"
#else
#include <cerrno>

// SIGBUS is POSIX's, which <csignal> need not declare.
#include <fcntl.h>
#include <signal.h>  // NOLINT(modernize-deprecated-headers)
#include <sys/resource.h>
#include <unistd.h>
#include <unwind.h>
#endif

namespace augusta::crash {

namespace {

// Everything from here to the handler runs inside it, so it allocates nothing,
// takes no lock and calls only async-signal-safe functions.

#ifdef _WIN32
using Descriptor = HANDLE;
#else
using Descriptor = int;
#endif

// What Install was given, set before any handler is: the subsystem a literal,
// the settings never freed, so a crash during exit still reads them. The Run
// ID and core location are views into them, empty where not given, and the
// log file opened ahead of any crash, if given.
std::string_view g_subsystem;
const Settings* g_settings = nullptr;
std::string_view g_run_id;
std::string_view g_core_location;
std::optional<Descriptor> g_log_file;

#ifdef _WIN32
constexpr std::array kFatalSignals{SIGSEGV, SIGILL, SIGFPE, SIGABRT};
#else
constexpr std::array kFatalSignals{SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
#endif

constexpr int kMaxFrames = 64;
// Room for a demangled name; Line cuts a longer one anyway.
constexpr std::size_t kSymbolCapacity = sizeof(Line);

constexpr std::int64_t kSecondsPerMinute = 60;
constexpr std::int64_t kSecondsPerHour = 60 * kSecondsPerMinute;
constexpr std::int64_t kSecondsPerDay = 24 * kSecondsPerHour;

constexpr std::uint64_t kDecimal = 10;
constexpr std::uint64_t kHexadecimal = 16;

// Appends to a Line, always keeping room for the newline Finish ends it
// with, so a line too long is cut rather than overrun.
class LineWriter {
 public:
  explicit LineWriter(Line& line) : line_(line) {}

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
  // line runs out, keeping room for after more characters after it, but always
  // closed.
  void PutQuoted(std::string_view text, std::size_t after = 0) {
    // The closing quote, what follows it and the newline.
    const std::size_t closing = 2 + after;
    Put('"');
    for (const char c : text) {
      const bool escaped = c == '"' || c == '\\';
      if (!Fits(escaped ? 2 : 1, closing)) {
        break;
      }
      if (escaped) {
        Put('\\', closing);
      }
      Put(c, closing);
    }
    Put('"');
  }

  std::string_view Finish() {
    line_[size_++] = '\n';
    return {line_.data(), size_};
  }

 private:
  Line& line_;
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

void PutCrashPrefix(LineWriter& writer, std::string_view subsystem, std::chrono::sys_seconds time) {
  PutTimestamp(writer, time);
  writer.Put(" CRITICAL subsystem=");
  writer.Put(subsystem);
  writer.Put(' ');
}

constexpr std::string_view kRunKey = " run=";

// How many characters PutRun writes for run_id.
std::size_t RunLength(std::string_view run_id) { return run_id.empty() ? 0 : kRunKey.size() + run_id.size(); }

// The Run's field, where logging's context puts it: last (ADR-0029).
void PutRun(LineWriter& writer, std::string_view run_id) {
  if (!run_id.empty()) {
    writer.Put(kRunKey);
    writer.Put(run_id);
  }
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

void WriteTo(Descriptor descriptor, std::string_view text) {
#ifdef _WIN32
  DWORD written = 0;
  WriteFile(descriptor, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
#else
  while (!text.empty()) {
    const auto written = write(descriptor, text.data(), text.size());
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

// A crash line to stdout, where the log sink writes, and to the Run's log file.
void WriteLine(std::string_view line) {
#ifdef _WIN32
  WriteTo(GetStdHandle(STD_OUTPUT_HANDLE), line);
#else
  WriteTo(STDOUT_FILENO, line);
#endif
  if (g_log_file) {
    WriteTo(*g_log_file, line);
  }
}

// The file at path opened for appending beside the log sink, which keeps it
// open too; nullopt if it cannot be.
std::optional<Descriptor> OpenForAppending(const std::filesystem::path& path) {
#ifdef _WIN32
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }
  return file;
#else
  // O_APPEND, so each write lands after the sink's last line, as the sink's
  // own lines do.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) - open(2) is variadic for its mode.
  const int file = open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
  if (file < 0) {
    return std::nullopt;
  }
  return file;
#endif
}

#ifndef _WIN32
// Where _Unwind_Backtrace's callback stores each frame it walks.
struct FrameSink {
  std::span<void*> frames;
  std::size_t depth = 0;
};

_Unwind_Reason_Code CollectFrame(_Unwind_Context* context, void* sink_arg) {
  auto& sink = *static_cast<FrameSink*>(sink_arg);
  if (sink.depth == sink.frames.size()) {
    return _URC_END_OF_STACK;
  }
  sink.frames[sink.depth] = reinterpret_cast<void*>(_Unwind_GetIP(context));
  ++sink.depth;
  return _URC_NO_REASON;
}
#endif

// The crashing thread's return addresses, innermost first, this handler's own
// frames included. On Linux the statically linked libunwind (ADR-0008) unwinds
// by the binary's unwind tables, so frames compiled without frame pointers are
// not lost. It is not listed as async-signal-safe, so Install walks
// once ahead of any crash: what it sets up lazily is then already done.
int CaptureFrames(std::span<void*> frames) {
#ifdef _WIN32
  return absl::GetStackTrace(frames.data(), static_cast<int>(frames.size()), 0);
#else
  FrameSink sink{.frames = frames};
  _Unwind_Backtrace(CollectFrame, &sink);
  return static_cast<int>(sink.depth);
#endif
}

void ReportCrash(int signal) {
  // time() rather than system_clock::now(): POSIX lists it as async-signal-safe.
  const std::chrono::sys_seconds now{std::chrono::seconds{std::time(nullptr)}};
  Line line{};
  WriteLine(FormatSignalLine(line, g_subsystem, now, signal, g_core_location, g_run_id));

  std::array<void*, kMaxFrames> frames{};
  const int depth = CaptureFrames(frames);
  std::array<char, kSymbolCapacity> symbol{};
  for (int index = 0; index < depth; ++index) {
    void* const pc = frames[static_cast<std::size_t>(index)];
    const bool resolved = absl::Symbolize(pc, symbol.data(), static_cast<int>(symbol.size()));
    WriteLine(FormatFrameLine(line, g_subsystem, now, index, reinterpret_cast<std::uintptr_t>(pc),
                              resolved ? symbol.data() : nullptr, g_run_id));
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

std::string_view FormatSignalLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int signal,
                                  std::string_view core, std::string_view run_id) {
  LineWriter writer(line);
  PutCrashPrefix(writer, subsystem, time);
  writer.Put("event=crash signal=");
  if (const std::string_view name = SignalName(signal); !name.empty()) {
    writer.Put(name);
  } else {
    writer.PutDigits(static_cast<std::uint64_t>(signal), kDecimal);
  }
  if (!core.empty()) {
    writer.Put(" core=");
    writer.PutQuoted(core, RunLength(run_id));
  }
  PutRun(writer, run_id);
  return writer.Finish();
}

std::string_view FormatFrameLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int index,
                                 std::uintptr_t pc, const char* symbol, std::string_view run_id) {
  LineWriter writer(line);
  PutCrashPrefix(writer, subsystem, time);
  writer.Put("event=crash_frame index=");
  writer.PutDigits(static_cast<std::uint64_t>(index), kDecimal);
  writer.Put(" pc=0x");
  writer.PutDigits(pc, kHexadecimal);
  if (symbol != nullptr) {
    writer.Put(" symbol=");
    // The Run's field kept whole: a line that loses it no longer says its Run.
    writer.PutQuoted(symbol, RunLength(run_id));
  }
  PutRun(writer, run_id);
  return writer.Finish();
}

void Install(const char* argv0, std::string_view subsystem, Settings settings) {
  g_subsystem = subsystem;
  g_settings = new Settings(std::move(settings));
  if (g_settings->run_id) {
    g_run_id = *g_settings->run_id;
  }
  if (g_settings->core_location) {
    g_core_location = *g_settings->core_location;
  }
  if (g_settings->log_file) {
    g_log_file = OpenForAppending(*g_settings->log_file);
  }
  absl::InitializeSymbolizer(argv0);
  // Its first call sets up what it needs (see CaptureFrames).
  std::array<void*, 1> warm_up{};
  CaptureFrames(warm_up);
#ifndef _WIN32
  RaiseCoreLimit();
#endif
  for (const int signal : kFatalSignals) {
    std::signal(signal, HandleFatalSignal);
  }
}

}  // namespace augusta::crash
