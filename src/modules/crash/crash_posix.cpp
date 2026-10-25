#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

#include "absl/debugging/symbolize.h"
#include "augusta/crash.h"
#include "crash_platform.h"

// SIGBUS is POSIX's, which <csignal> need not declare; open's flags POSIX's
// too. macOS's SDK declares both in <sys/...>, which these include.
#ifdef __APPLE__
#include <pthread.h>
#include <sys/fcntl.h>
#include <sys/signal.h>
#else
#include <fcntl.h>
#include <signal.h>  // NOLINT(modernize-deprecated-headers)
#endif
#include <sys/resource.h>
#include <time.h>  // NOLINT(modernize-deprecated-headers) - POSIX's nanosleep.
#include <unistd.h>
#include <unwind.h>

namespace augusta::crash {

namespace {

// Everything from here to InstallHandlers runs inside the handler, so it
// allocates nothing, takes no lock and calls only async-signal-safe functions.

constexpr std::array kFatalSignals{SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

// The calling thread's system-wide ID, never 0: Linux's TID, macOS's thread ID
// (which has no gettid).
#ifdef __APPLE__
using ThreadId = std::uint64_t;
ThreadId CurrentThread() {
  ThreadId id = 0;
  pthread_threadid_np(nullptr, &id);
  return id;
}
#else
using ThreadId = pid_t;
ThreadId CurrentThread() { return gettid(); }
#endif

// The thread reporting a crash, or 0 until one does. A second thread that
// crashes meanwhile waits for that report to end the process, so the two
// threads' lines never interleave; a fault in the report itself ends the
// process at once. Lock-free, as a handler needs.
std::atomic<ThreadId> g_reporter{0};
static_assert(std::atomic<ThreadId>::is_always_lock_free, "the fatal-signal handler takes the report");

// The Run's log file, opened ahead of any crash, if given.
std::optional<int> g_log_file;

// Room for a demangled name; Line cuts a longer one anyway.
constexpr std::size_t kSymbolCapacity = sizeof(Line);

void WriteTo(int descriptor, std::string_view text) {
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
}

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
  // NOLINTNEXTLINE(misc-include-cleaner) - <unwind.h>'s, which on macOS declares it in an internal header.
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

// Every fatal signal back to its default action.
void RestoreDefaultActions() {
  for (const int fatal : kFatalSignals) {
    std::signal(fatal, SIG_DFL);
  }
}

// Until the process ends, which the report another thread is writing ends it
// in. nanosleep, not pause: it is async-signal-safe, and the server's seccomp
// profile allows the clock_nanosleep it makes (ADR-0057).
[[noreturn]] void WaitForTheReport() {
  for (;;) {
    const timespec second{.tv_sec = 1, .tv_nsec = 0};
    // NOLINTNEXTLINE(misc-include-cleaner) - <time.h>'s, which on macOS declares it in an internal header.
    nanosleep(&second, nullptr);
  }
}

extern "C" void HandleFatalSignal(int signal) {
  const ThreadId self = CurrentThread();
  ThreadId reporter = 0;
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
  WriteSignalLines(signal, CaptureFrames);
  RestoreDefaultActions();
  // Pending until the handler returns, then delivered to the default action:
  // the process dies of it and the kernel dumps its core. Not as PID 1, whose
  // own signals the kernel drops, hence the image's init (ADR-0047).
  std::raise(signal);
}

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

}  // namespace

void OpenLogFile(const std::filesystem::path& path) {
  // O_APPEND, so each write lands after the sink's last line, as the sink's
  // own lines do.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) - open(2) is variadic for its mode.
  if (const int file = open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644); file >= 0) {
    g_log_file = file;
  }
}

void WriteLine(std::string_view line) {
  WriteTo(STDOUT_FILENO, line);
  if (g_log_file) {
    WriteTo(*g_log_file, line);
  }
}

// One line per frame, each naming its function, from the symbol table the
// binary keeps.
void WriteFrameLines(std::span<void* const> frames, std::string_view subsystem, std::chrono::sys_seconds now,
                     std::string_view context, Line& line) {
  std::array<char, kSymbolCapacity> symbol{};
  for (std::size_t index = 0; index < frames.size(); ++index) {
    void* const pc = frames[index];
    const bool resolved = absl::Symbolize(pc, symbol.data(), static_cast<int>(symbol.size()));
    WriteLine(FormatFrameLine(line, subsystem, now, static_cast<int>(index), reinterpret_cast<std::uintptr_t>(pc),
                              resolved ? symbol.data() : nullptr, context));
  }
}

void InstallHandlers(const char* argv0, const Settings& /*settings*/) {
  absl::InitializeSymbolizer(argv0);
  // Its first call sets up what it needs (see CaptureFrames).
  std::array<void*, 1> warm_up{};
  CaptureFrames(warm_up);
  RaiseCoreLimit();
  for (const int signal : kFatalSignals) {
    std::signal(signal, HandleFatalSignal);
  }
}

}  // namespace augusta::crash
