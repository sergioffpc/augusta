#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <boost/uuid/uuid_io.hpp>
#include <windows.h>

#include "augusta/crash.h"
#include "crash_platform.h"
// After <windows.h>, whose types it uses.
#include <dbghelp.h>
#include <excpt.h>

namespace augusta::crash {

namespace {

// Everything from here to InstallHandlers runs inside the handler or the
// unhandled-exception filter, so it allocates nothing, takes no lock and calls
// only async-signal-safe functions.

// How many characters a Run ID is in its canonical form.
constexpr std::size_t kRunIdLength = 36;

// What InstallHandlers was given, set before any handler is: the Run ID (for
// the minidump) in its canonical form, empty where not given, and the
// minidump's path, or nullptr if none.
std::array<char, kRunIdLength> g_run_id_text{};
std::string_view g_run_id;
const wchar_t* g_minidump_file = nullptr;

// The Run's log file, opened ahead of any crash, or INVALID_HANDLE_VALUE.
HANDLE g_log_file = INVALID_HANDLE_VALUE;

// Set by the first crash to report, so a second - in another thread, or in the
// report itself - ends the process at once.
std::atomic_flag g_reporting;

// Only SIGABRT is a signal there: a fault is an exception, which the
// unhandled-exception filter meets in every thread. A handler for SIGSEGV,
// SIGILL or SIGFPE would have the CRT take the main thread's exception first,
// leaving the minidump no exception to name.
constexpr std::array kFatalSignals{SIGABRT};

// Room for a module's file name; Line cuts a longer one anyway.
constexpr std::size_t kSymbolCapacity = sizeof(Line);

void WriteTo(HANDLE descriptor, std::string_view text) {
  DWORD written = 0;
  WriteFile(descriptor, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

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

// The unhandled-exception filter: the crash's lines, then its minidump.
LONG WINAPI HandleUnhandledException(EXCEPTION_POINTERS* exception) {
  if (g_reporting.test_and_set()) {
    return EXCEPTION_EXECUTE_HANDLER;
  }
  WriteExceptionLines(exception->ExceptionRecord->ExceptionCode, [exception](std::span<void*> frames) {
    return CaptureFrames(*exception->ContextRecord, frames);
  });
  WriteMinidump(exception);
  // Ends the process with the exception's code, as Windows would have, but
  // without Windows Error Reporting: the dump is written.
  return EXCEPTION_EXECUTE_HANDLER;
}

// Every fatal signal back to its default action.
void RestoreDefaultActions() {
  for (const int fatal : kFatalSignals) {
    std::signal(fatal, SIG_DFL);
  }
}

extern "C" void HandleFatalSignal(int signal) {
  // Back to the default first, so a second fault - in another thread, or in
  // this report - ends the process at once instead of reporting again.
  RestoreDefaultActions();
  if (!g_reporting.test_and_set()) {
    // From the stack the handler runs on, the aborting thread's.
    WriteSignalLines(signal, [](std::span<void*> frames) {
      CONTEXT context{};
      RtlCaptureContext(&context);
      return CaptureFrames(context, frames);
    });
    WriteMinidump(nullptr);
  }
  // Pending until the handler returns, then delivered to the default action,
  // which ends the process.
  std::raise(signal);
}

}  // namespace

void OpenLogFile(const std::filesystem::path& path) {
  g_log_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void WriteLine(std::string_view line) {
  WriteTo(GetStdHandle(STD_OUTPUT_HANDLE), line);
  if (g_log_file != INVALID_HANDLE_VALUE) {
    WriteTo(g_log_file, line);
  }
}

// One line per frame, each naming its module and offset, for the release's
// PDB to symbolize (ADR-0055).
void WriteFrameLines(std::span<void* const> frames, std::string_view subsystem, std::chrono::sys_seconds now,
                     std::string_view context, Line& line) {
  std::array<char, kSymbolCapacity> module{};
  for (std::size_t index = 0; index < frames.size(); ++index) {
    const auto pc = reinterpret_cast<std::uintptr_t>(frames[index]);
    std::uintptr_t base = 0;
    const char* const name = ModuleOf(frames[index], module, base);
    WriteLine(FormatModuleFrameLine(line, subsystem, now, static_cast<int>(index), pc, name, pc - base, context));
  }
}

void InstallHandlers(const char* /*argv0*/, const Settings& settings) {
  if (settings.run_id) {
    const std::string text = boost::uuids::to_string(*settings.run_id);
    g_run_id = {g_run_id_text.data(), text.copy(g_run_id_text.data(), g_run_id_text.size())};
  }
  if (settings.minidump_file) {
    // Made now, since the filter makes nothing it can do without; if it cannot
    // be, the minidump is not written, as when the file cannot be created.
    std::error_code ignored;
    std::filesystem::create_directories(settings.minidump_file->parent_path(), ignored);
    g_minidump_file = settings.minidump_file->c_str();
  }
  SetUnhandledExceptionFilter(HandleUnhandledException);
  for (const int signal : kFatalSignals) {
    std::signal(signal, HandleFatalSignal);
  }
}

}  // namespace augusta::crash
