#include "augusta/crash.h"

#include <array>
#include <atomic>
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

#include "augusta/logging.h"

#ifdef _WIN32
#include <windows.h>
// After <windows.h>, whose types it uses.
#include <system_error>

#include <dbghelp.h>
#include <excpt.h>
#else
#include <cerrno>

#include "absl/debugging/symbolize.h"

// SIGBUS is POSIX's, which <csignal> need not declare.
#include <fcntl.h>
#include <signal.h>  // NOLINT(modernize-deprecated-headers)
#include <sys/resource.h>
#include <time.h>  // NOLINT(modernize-deprecated-headers) - POSIX's nanosleep.
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
// ID (for the minidump) and core location are views into them, empty where not
// given, and the log file opened ahead of any crash, if given.
std::string_view g_subsystem;
const Settings* g_settings = nullptr;
std::string_view g_run_id;
std::string_view g_core_location;
std::optional<Descriptor> g_log_file;

#ifdef _WIN32
// The minidump's path, from the settings, or nullptr if none.
const wchar_t* g_minidump_file = nullptr;
// Set by the first crash to report, so a second - in another thread, or in the
// report itself - ends the process at once.
std::atomic_flag g_reporting;

// Only SIGABRT is a signal there: a fault is an exception, which the
// unhandled-exception filter meets in every thread. A handler for SIGSEGV,
// SIGILL or SIGFPE would have the CRT take the main thread's exception first,
// leaving the minidump no exception to name.
constexpr std::array kFatalSignals{SIGABRT};
#else
constexpr std::array kFatalSignals{SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

// The thread reporting a crash, or 0 until one does. A second thread that
// crashes meanwhile waits for that report to end the process, so the two
// threads' lines never interleave; a fault in the report itself ends the
// process at once. Lock-free, as a handler needs.
std::atomic<pid_t> g_reporter{0};
static_assert(std::atomic<pid_t>::is_always_lock_free, "the fatal-signal handler takes the report");
#endif

constexpr int kMaxFrames = 64;
// Room for a demangled name, or a module's file name; Line cuts a longer one
// anyway.
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

// What every frame line starts with: `... event=crash_frame index=<index>
// pc=0x<pc>`.
void PutFramePrefix(LineWriter& writer, std::string_view subsystem, std::chrono::sys_seconds time, int index,
                    std::uintptr_t pc) {
  PutCrashPrefix(writer, subsystem, time);
  writer.Put("event=crash_frame index=");
  writer.PutDigits(static_cast<std::uint64_t>(index), kDecimal);
  writer.Put(" pc=0x");
  writer.PutDigits(pc, kHexadecimal);
}

// How many digits PutDigits writes for value in hexadecimal.
std::size_t HexLength(std::uint64_t value) {
  std::size_t length = 1;
  while (value >= kHexadecimal) {
    value /= kHexadecimal;
    ++length;
  }
  return length;
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

#ifdef _WIN32
// The return addresses of the stack context stopped in, innermost first, frame
// 0 its own instruction: unwound by each module's unwind tables, as the
// debugger does, so a crash's frames start where it faulted rather than in
// the exception's dispatch. x64's unwinding, the only Windows target.
int CaptureFrames(const CONTEXT& start, std::span<void*> frames) {
  CONTEXT context = start;
  std::size_t depth = 0;
  while (depth < frames.size() && context.Rip != 0) {
    frames[depth++] = reinterpret_cast<void*>(context.Rip);
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
    if (function == nullptr) {
      // A leaf function, which keeps nothing on the stack but its return
      // address.
      context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
      context.Rsp += sizeof(DWORD64);
    } else {
      void* handler_data = nullptr;
      DWORD64 establisher_frame = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context, &handler_data,
                       &establisher_frame, nullptr);
    }
  }
  return static_cast<int>(depth);
}

// The file name of the module that holds pc, written into name, and the
// module's base address into base; nullptr if no module holds it.
const char* ModuleOf(void* pc, std::span<char> name, std::uintptr_t& base) {
  HMODULE module = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         static_cast<LPCWSTR>(pc), &module) == FALSE) {
    return nullptr;
  }
  std::array<wchar_t, MAX_PATH> path{};
  const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0) {
    return nullptr;
  }
  const std::wstring_view full(path.data(), length);
  // After the last separator; the whole path if there is none (npos + 1 is 0).
  const std::wstring_view file = full.substr(full.find_last_of(L"\\/") + 1);
  const int written = WideCharToMultiByte(CP_UTF8, 0, file.data(), static_cast<int>(file.size()), name.data(),
                                          static_cast<int>(name.size() - 1), nullptr, nullptr);
  if (written <= 0) {
    return nullptr;
  }
  name[static_cast<std::size_t>(written)] = '\0';
  base = reinterpret_cast<std::uintptr_t>(module);
  return name.data();
}

// One line per frame, each naming its module and offset, for the release's
// PDB to symbolize (ADR-0055).
void WriteFrameLines(std::span<void* const> frames, std::chrono::sys_seconds now, std::string_view context,
                     Line& line) {
  std::array<char, kSymbolCapacity> module{};
  for (std::size_t index = 0; index < frames.size(); ++index) {
    const auto pc = reinterpret_cast<std::uintptr_t>(frames[index]);
    std::uintptr_t base = 0;
    const char* const name = ModuleOf(frames[index], module, base);
    WriteLine(FormatModuleFrameLine(line, g_subsystem, now, static_cast<int>(index), pc, name, pc - base, context));
  }
}

// The settings' minidump, if any: the stacks, the data segments and the memory
// they point to, but not the whole process (ADR-0055), with the Run ID in its
// user stream, so a renamed dump still names its Run. exception is the crash's,
// or nullptr for an abort, whose dump shows the aborting thread as it is.
void WriteMinidump(EXCEPTION_POINTERS* exception) {
  if (g_minidump_file == nullptr) {
    return;
  }
  HANDLE file = CreateFileW(g_minidump_file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  MINIDUMP_EXCEPTION_INFORMATION exception_information{
      .ThreadId = GetCurrentThreadId(), .ExceptionPointers = exception, .ClientPointers = FALSE};
  MINIDUMP_USER_STREAM run{.Type = kRunIdStreamType,
                           .BufferSize = static_cast<ULONG>(g_run_id.size()),
                           // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) - dbghelp only reads it.
                           .Buffer = const_cast<char*>(g_run_id.data())};
  MINIDUMP_USER_STREAM_INFORMATION streams{.UserStreamCount = g_run_id.empty() ? 0U : 1U, .UserStreamArray = &run};
  MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                    static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory),
                    exception != nullptr ? &exception_information : nullptr, &streams, nullptr);
  CloseHandle(file);
}
#else
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

// The crashing thread's return addresses, innermost first, this handler's own
// frames included. The statically linked libunwind (ADR-0008) unwinds by the
// binary's unwind tables, so frames compiled without frame pointers are not
// lost. It is not listed as async-signal-safe, so Install walks once ahead of
// any crash: what it sets up lazily is then already done.
int CaptureFrames(std::span<void*> frames) {
  FrameSink sink{.frames = frames};
  _Unwind_Backtrace(CollectFrame, &sink);
  return static_cast<int>(sink.depth);
}

// One line per frame, each naming its function, from the symbol table the
// binary keeps.
void WriteFrameLines(std::span<void* const> frames, std::chrono::sys_seconds now, std::string_view context,
                     Line& line) {
  std::array<char, kSymbolCapacity> symbol{};
  for (std::size_t index = 0; index < frames.size(); ++index) {
    void* const pc = frames[index];
    const bool resolved = absl::Symbolize(pc, symbol.data(), static_cast<int>(symbol.size()));
    WriteLine(FormatFrameLine(line, g_subsystem, now, static_cast<int>(index), reinterpret_cast<std::uintptr_t>(pc),
                              resolved ? symbol.data() : nullptr, context));
  }
}
#endif

// time() rather than system_clock::now(): POSIX lists it as async-signal-safe.
std::chrono::sys_seconds Now() { return std::chrono::sys_seconds{std::chrono::seconds{std::time(nullptr)}}; }

void ReportCrash(int signal) {
  const std::chrono::sys_seconds now = Now();
  logging::ContextText context_text{};
  const std::string_view log_context = logging::CopyContext(context_text);
  Line line{};
  WriteLine(FormatSignalLine(line, g_subsystem, now, signal, g_core_location, log_context));

  std::array<void*, kMaxFrames> frames{};
#ifdef _WIN32
  CONTEXT context{};
  RtlCaptureContext(&context);
  const int depth = CaptureFrames(context, frames);
#else
  const int depth = CaptureFrames(frames);
#endif
  WriteFrameLines(std::span<void* const>(frames.data(), static_cast<std::size_t>(depth)), now, log_context, line);
#ifdef _WIN32
  WriteMinidump(nullptr);
#endif
}

#ifdef _WIN32
// The unhandled-exception filter: the crash's lines, then its minidump.
LONG WINAPI HandleUnhandledException(EXCEPTION_POINTERS* exception) {
  if (g_reporting.test_and_set()) {
    return EXCEPTION_EXECUTE_HANDLER;
  }
  const std::chrono::sys_seconds now = Now();
  logging::ContextText context_text{};
  const std::string_view context = logging::CopyContext(context_text);
  Line line{};
  WriteLine(FormatExceptionLine(line, g_subsystem, now, exception->ExceptionRecord->ExceptionCode, context));
  std::array<void*, kMaxFrames> frames{};
  const int depth = CaptureFrames(*exception->ContextRecord, frames);
  WriteFrameLines(std::span<void* const>(frames.data(), static_cast<std::size_t>(depth)), now, context, line);
  WriteMinidump(exception);
  // Ends the process with the exception's code, as Windows would have, but
  // without Windows Error Reporting: the dump is written.
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// Every fatal signal back to its default action.
void RestoreDefaultActions() {
  for (const int fatal : kFatalSignals) {
    std::signal(fatal, SIG_DFL);
  }
}

#ifndef _WIN32
// Until the process ends, which the report another thread is writing ends it
// in. nanosleep, not pause: it is async-signal-safe, and the server's seccomp
// profile allows the clock_nanosleep it makes (ADR-0057).
[[noreturn]] void WaitForTheReport() {
  for (;;) {
    const timespec second{.tv_sec = 1, .tv_nsec = 0};
    nanosleep(&second, nullptr);
  }
}
#endif

extern "C" void HandleFatalSignal(int signal) {
#ifdef _WIN32
  // Back to the default first, so a second fault - in another thread, or in
  // this report - ends the process at once instead of reporting again.
  RestoreDefaultActions();
  if (!g_reporting.test_and_set()) {
    ReportCrash(signal);
  }
#else
  const pid_t self = gettid();
  pid_t reporter = 0;
  if (!g_reporter.compare_exchange_strong(reporter, self)) {
    if (reporter != self) {
      WaitForTheReport();
    }
    // A fault in the report itself, of another signal than the one reported
    // (that one is blocked while it is handled, so the kernel ends the process
    // of it): its default action ends the process at once.
    RestoreDefaultActions();
    std::raise(signal);
    return;
  }
  ReportCrash(signal);
  RestoreDefaultActions();
#endif
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
                                  std::string_view core, std::string_view context) {
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
    writer.PutQuoted(core, context.size());
  }
  writer.Put(context);
  return writer.Finish();
}

std::string_view FormatFrameLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int index,
                                 std::uintptr_t pc, const char* symbol, std::string_view context) {
  LineWriter writer(line);
  PutFramePrefix(writer, subsystem, time, index, pc);
  if (symbol != nullptr) {
    writer.Put(" symbol=");
    // The context kept whole: a line that loses it no longer says its Run.
    writer.PutQuoted(symbol, context.size());
  }
  writer.Put(context);
  return writer.Finish();
}

std::string_view FormatModuleFrameLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int index,
                                       std::uintptr_t pc, const char* module, std::uintptr_t offset,
                                       std::string_view context) {
  LineWriter writer(line);
  PutFramePrefix(writer, subsystem, time, index, pc);
  if (module != nullptr) {
    // The offset and the context kept whole: a frame without its offset names
    // no instruction.
    constexpr std::string_view kOffsetKey = " offset=0x";
    writer.Put(" module=");
    writer.PutQuoted(module, kOffsetKey.size() + HexLength(offset) + context.size());
    writer.Put(kOffsetKey);
    writer.PutDigits(offset, kHexadecimal);
  }
  writer.Put(context);
  return writer.Finish();
}

std::string_view FormatExceptionLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time,
                                     std::uint32_t code, std::string_view context) {
  constexpr int kCodeDigits = 8;
  LineWriter writer(line);
  PutCrashPrefix(writer, subsystem, time);
  writer.Put("event=crash exception=0x");
  writer.PutDigits(code, kHexadecimal, kCodeDigits);
  writer.Put(context);
  return writer.Finish();
}

void Install([[maybe_unused]] const char* argv0, std::string_view subsystem, Settings settings) {
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
#ifdef _WIN32
  if (g_settings->minidump_file) {
    // Made now, since the filter makes nothing it can do without; if it cannot
    // be, the minidump is not written, as when the file cannot be created.
    std::error_code ignored;
    std::filesystem::create_directories(g_settings->minidump_file->parent_path(), ignored);
    g_minidump_file = g_settings->minidump_file->c_str();
  }
  SetUnhandledExceptionFilter(HandleUnhandledException);
#else
  absl::InitializeSymbolizer(argv0);
  // Its first call sets up what it needs (see CaptureFrames).
  std::array<void*, 1> warm_up{};
  CaptureFrames(warm_up);
  RaiseCoreLimit();
#endif
  for (const int signal : kFatalSignals) {
    std::signal(signal, HandleFatalSignal);
  }
}

}  // namespace augusta::crash
