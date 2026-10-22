#include "augusta/crash.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <gtest/gtest.h>

#include "augusta/logging.h"

#ifdef _WIN32
#include <windows.h>
// After <windows.h>, whose types it uses.
#include <cstdint>
#include <iterator>
#include <thread>

#include <dbghelp.h>
#else
#include <unistd.h>
#endif

namespace {

using augusta::crash::FormatExceptionLine;
using augusta::crash::FormatFrameLine;
using augusta::crash::FormatModuleFrameLine;
using augusta::crash::FormatSignalLine;
using augusta::crash::Line;

// The subsystem augustad's crash lines name.
constexpr std::string_view kServer = "server";

// 2024-02-01T12:00:00Z
constexpr std::chrono::sys_seconds kFixedTime{std::chrono::seconds{1706788800}};

// What logging::FormatLine writes for message at time, as the console sink ends
// it: the crash lines must read like every other CRITICAL line (ADR-0029).
std::string LoggedLine(std::chrono::sys_seconds time, std::string_view message) {
  return augusta::logging::FormatLine(time, augusta::logging::Severity::kCritical, message, false) + "\n";
}

// Requirements: NFR-10
TEST(CrashFormat, SignalLineIsACriticalLogLine) {
  Line line{};
  EXPECT_EQ(FormatSignalLine(line, kServer, kFixedTime, SIGSEGV),
            LoggedLine(kFixedTime, "subsystem=server event=crash signal=SIGSEGV"));
}

// Requirements: NFR-10
TEST(CrashFormat, SignalWithoutANameIsWrittenAsItsNumber) {
  Line line{};
  EXPECT_EQ(FormatSignalLine(line, kServer, kFixedTime, 77),
            LoggedLine(kFixedTime, "subsystem=server event=crash signal=77"));
}

// Requirements: NFR-10
TEST(CrashFormat, LinesNameTheSubsystemTheyAreGiven) {
  Line line{};
  EXPECT_EQ(FormatSignalLine(line, "client", kFixedTime, SIGABRT),
            LoggedLine(kFixedTime, "subsystem=client event=crash signal=SIGABRT"));
  EXPECT_EQ(FormatFrameLine(line, "client", kFixedTime, 1, 0x20U, nullptr),
            LoggedLine(kFixedTime, "subsystem=client event=crash_frame index=1 pc=0x20"));
}

// The client's crash lines end in the Run's run=, as its context gives every
// other line (ADR-0029), and its crash line says where the core is.
// Requirements: NFR-10
TEST(CrashFormat, ClientLinesNameTheCoreAndEndInTheLogsContext) {
  Line line{};
  constexpr std::string_view kContext = " run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f session=s1 match=m1";
  EXPECT_EQ(FormatSignalLine(line, "client", kFixedTime, SIGSEGV, "coredumpctl dump augustac", kContext),
            LoggedLine(kFixedTime,
                       "subsystem=client event=crash signal=SIGSEGV core=\"coredumpctl dump augustac\" "
                       "run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f session=s1 match=m1"));
  EXPECT_EQ(FormatFrameLine(line, "client", kFixedTime, 2, 0x30U, "main", kContext),
            LoggedLine(kFixedTime,
                       "subsystem=client event=crash_frame index=2 pc=0x30 symbol=\"main\" "
                       "run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f session=s1 match=m1"));
}

// Requirements: NFR-10
TEST(CrashFormat, FrameLineNamesIndexAddressAndQuotedSymbol) {
  Line line{};
  EXPECT_EQ(FormatFrameLine(line, kServer, kFixedTime, 3, 0x7f00deadbeefU, "augusta::host::Host::Step(int)"),
            LoggedLine(kFixedTime,
                       "subsystem=server event=crash_frame index=3 pc=0x7f00deadbeef "
                       "symbol=\"augusta::host::Host::Step(int)\""));
}

// Requirements: NFR-10
TEST(CrashFormat, UnresolvedFrameHasNoSymbol) {
  Line line{};
  EXPECT_EQ(FormatFrameLine(line, kServer, kFixedTime, 0, 0x10U, nullptr),
            LoggedLine(kFixedTime, "subsystem=server event=crash_frame index=0 pc=0x10"));
}

// Requirements: NFR-10
TEST(CrashFormat, QuotesAndBackslashesInASymbolAreEscaped) {
  Line line{};
  // The symbol f("\") is written "f(\"\\\")".
  EXPECT_EQ(FormatFrameLine(line, kServer, kFixedTime, 0, 0x10U, "f(\"\\\")"),
            LoggedLine(kFixedTime, "subsystem=server event=crash_frame index=0 pc=0x10 symbol=\"f(\\\"\\\\\\\")\""));
}

// A Windows frame names the module it is in and the offset into it, which the
// release's PDB symbolizes later: the zip carries none (ADR-0055).
// Requirements: NFR-10
TEST(CrashFormat, WindowsFrameLineNamesModuleAndOffset) {
  Line line{};
  EXPECT_EQ(FormatModuleFrameLine(line, "client", kFixedTime, 4, 0x7ff6a0011a2bU, "augustac.exe", 0x11a2bU,
                                  " run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f"),
            LoggedLine(kFixedTime,
                       "subsystem=client event=crash_frame index=4 pc=0x7ff6a0011a2b module=\"augustac.exe\" "
                       "offset=0x11a2b run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f"));
}

// Requirements: NFR-10
TEST(CrashFormat, WindowsFrameOutsideAnyModuleHasNoModule) {
  Line line{};
  EXPECT_EQ(FormatModuleFrameLine(line, kServer, kFixedTime, 0, 0x10U, nullptr, 0),
            LoggedLine(kFixedTime, "subsystem=server event=crash_frame index=0 pc=0x10"));
}

// Requirements: NFR-10
TEST(CrashFormat, ModuleTooLongForTheLineKeepsOffsetAndRunWhole) {
  Line line{};
  const std::string module(line.size() * 2, 'm');
  const std::string_view written = FormatModuleFrameLine(line, "client", kFixedTime, 0, 0x10U, module.c_str(), 0x20U,
                                                         " run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f");
  EXPECT_EQ(written.size(), line.size());
  EXPECT_TRUE(written.ends_with("mm\" offset=0x20 run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f\n")) << written;
}

// A Windows crash is an exception, named by its code as WinDbg shows it.
// Requirements: NFR-10
TEST(CrashFormat, ExceptionLineNamesItsCode) {
  Line line{};
  EXPECT_EQ(FormatExceptionLine(line, "client", kFixedTime, 0xC0000005U, " run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f"),
            LoggedLine(kFixedTime,
                       "subsystem=client event=crash exception=0xc0000005 run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f"));
}

// Requirements: NFR-10
TEST(CrashFormat, SymbolTooLongForTheLineIsCutButStillClosed) {
  Line line{};
  const std::string symbol(line.size() * 2, 'x');
  const std::string_view written = FormatFrameLine(line, kServer, kFixedTime, 0, 0x10U, symbol.c_str());
  EXPECT_EQ(written.size(), line.size());
  EXPECT_TRUE(written.ends_with("xx\"\n"));
}

// Requirements: NFR-10
TEST(CrashFormat, SymbolTooLongForTheLineKeepsTheRunWhole) {
  Line line{};
  const std::string symbol(line.size() * 2, 'x');
  const std::string_view written = FormatFrameLine(line, "client", kFixedTime, 0, 0x10U, symbol.c_str(),
                                                   " run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f");
  EXPECT_EQ(written.size(), line.size());
  EXPECT_TRUE(written.ends_with("xx\" run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f\n"));
}

#ifdef _WIN32
// What a defect does on Windows: an access violation, which no handler takes.
// On a thread of its own, since gtest takes an SEH exception in the test's
// thread before the unhandled-exception filter could.
void Crash() {
  std::thread([] {
    volatile std::uintptr_t address = 0;
    // NOLINTNEXTLINE(performance-no-int-to-ptr) - a null pointer the compiler cannot see, to fault on.
    *reinterpret_cast<volatile int*>(address) = 1;
  }).join();
}

// The crash line Crash leaves.
constexpr std::string_view kCrashEvent = "event=crash exception=0xc0000005";
#else
void Crash() { std::raise(SIGSEGV); }

constexpr std::string_view kCrashEvent = "event=crash signal=SIGSEGV";
#endif

// The handler writes where the log sink does, stdout; a death test reads
// stderr, so the dying child points one at the other first. Installed as
// subsystem with settings, then crashes by crash. Given a Run, its ID is in the
// log's context first, as the client's BeginRun puts it.
void CrashWithStdoutOnStderr(std::string_view subsystem = kServer, augusta::crash::Settings settings = {},
                             void (*crash)() = Crash) {
#ifdef _WIN32
  SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE));
#else
  dup2(STDERR_FILENO, STDOUT_FILENO);
#endif
  if (settings.run_id) {
    augusta::logging::SetContext(augusta::logging::ContextField::kRun, boost::uuids::to_string(*settings.run_id));
  }
  augusta::crash::Install("augusta_crash_tests", subsystem, std::move(settings));
  crash();
}

// A Run's ID, in its canonical form.
constexpr std::string_view kRunId = "0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f";

// kRunId as the ID the client holds.
boost::uuids::uuid RunId() { return boost::uuids::string_generator()(std::string(kRunId)); }

// A new, empty directory of the test's own under the temporary directory,
// which the child that dies makes again (on Windows it reruns the test).
std::filesystem::path EmptyDirectory(std::string_view name) {
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "augusta_crash_tests" / name;
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  return directory;
}

// The lines of the file at path, without their line feeds.
std::vector<std::string> ReadLines(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<std::string> lines;
  for (std::string line; std::getline(file, line);) {
    lines.push_back(line);
  }
  return lines;
}

// gtest's regular expressions on Windows have no character classes, hence the
// plain substrings.
// Requirements: NFR-10
TEST(CrashHandlerDeathTest, FatalSignalIsLoggedBeforeTheProcessDies) {
  EXPECT_DEATH(CrashWithStdoutOnStderr(), "CRITICAL subsystem=server " + std::string(kCrashEvent));
}

// Requirements: NFR-10
TEST(CrashHandlerDeathTest, FatalSignalLogsTheStack) {
  EXPECT_DEATH(CrashWithStdoutOnStderr(), "CRITICAL subsystem=server event=crash_frame index=0 pc=0x");
}

// Requirements: NFR-10
TEST(CrashHandlerDeathTest, AbortIsLoggedBeforeTheProcessDies) {
  EXPECT_DEATH(CrashWithStdoutOnStderr(kServer, {}, [] { std::abort(); }),
               "CRITICAL subsystem=server event=crash signal=SIGABRT");
}

// A client's crash lines end in every field of the log's context, as its
// other lines do (ADR-0029), so a crash maps to its Session and Match.
// Requirements: NFR-10
TEST(CrashHandlerDeathTest, ACrashsLinesEndInTheLogsContext) {
  const std::filesystem::path log = EmptyDirectory("ACrashsLinesEndInTheLogsContext") / "run.log";

  EXPECT_DEATH(
      {
        augusta::logging::SetContext(augusta::logging::ContextField::kSession, "0190f2a4-1b2c-7d3e-8f40-000000000001");
        augusta::logging::SetContext(augusta::logging::ContextField::kMatch, "0190f2a4-1b2c-7d3e-8f40-000000000002");
        CrashWithStdoutOnStderr(
            "client",
            {.minidump_file = std::nullopt, .run_id = RunId(), .log_file = log, .core_location = std::nullopt});
      },
      std::string(kCrashEvent));

  const std::vector<std::string> lines = ReadLines(log);
  ASSERT_GE(lines.size(), 2U);
  for (const std::string& line : lines) {
    EXPECT_TRUE(
        line.ends_with(" run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f session=0190f2a4-1b2c-7d3e-8f40-000000000001 "
                       "match=0190f2a4-1b2c-7d3e-8f40-000000000002"))
        << line;
  }
}

#ifdef _WIN32
// The Run ID the user stream of the minidump at path holds, or nullopt if it
// has none.
std::optional<std::string> RunIdStream(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<char> dump{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  MINIDUMP_DIRECTORY* directory = nullptr;
  void* stream = nullptr;
  ULONG size = 0;
  if (dump.empty() ||
      MiniDumpReadDumpStream(dump.data(), augusta::crash::kRunIdStreamType, &directory, &stream, &size) == FALSE) {
    return std::nullopt;
  }
  return std::string(static_cast<const char*>(stream), size);
}

// A Windows client's crash writes its minidump, named by the Run, which says
// its Run though renamed, and its lines reach the Run's log, each frame naming
// its module and offset into it (ADR-0055).
// Requirements: NFR-10
TEST(CrashHandlerDeathTest, UnhandledExceptionWritesTheRunsMinidumpAndLog) {
  const std::filesystem::path directory = EmptyDirectory("UnhandledExceptionWritesTheRunsMinidumpAndLog");
  const std::filesystem::path log = directory / "logs" / "augustac-0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f.log";
  const std::filesystem::path minidump = directory / "dumps" / "augustac-0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f.dmp";
  std::filesystem::create_directories(log.parent_path());

  EXPECT_DEATH(
      CrashWithStdoutOnStderr(
          "client", {.minidump_file = minidump, .run_id = RunId(), .log_file = log, .core_location = std::nullopt}),
      "event=crash exception=0xc0000005");

  ASSERT_TRUE(std::filesystem::exists(minidump));
  EXPECT_EQ(RunIdStream(minidump), kRunId);
  const std::vector<std::string> lines = ReadLines(log);
  ASSERT_GE(lines.size(), 2U);
  EXPECT_EQ(lines[0].substr(lines[0].find(' ')),
            " CRITICAL subsystem=client event=crash exception=0xc0000005 run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f");
  EXPECT_TRUE(lines[1].contains(" CRITICAL subsystem=client event=crash_frame index=0 pc=0x")) << lines[1];
  EXPECT_TRUE(lines[1].contains(" module=\"augusta_crash_tests.exe\" offset=0x")) << lines[1];
  EXPECT_TRUE(lines[1].ends_with(" run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f")) << lines[1];
}

// An abort is no exception, but leaves the same minidump.
// Requirements: NFR-10
TEST(CrashHandlerDeathTest, AbortWritesTheRunsMinidump) {
  const std::filesystem::path minidump =
      EmptyDirectory("AbortWritesTheRunsMinidump") / "dumps" / "augustac-0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f.dmp";

  EXPECT_DEATH(
      CrashWithStdoutOnStderr(
          "client",
          {.minidump_file = minidump, .run_id = RunId(), .log_file = std::nullopt, .core_location = std::nullopt},
          [] { std::abort(); }),
      "event=crash signal=SIGABRT");

  EXPECT_EQ(RunIdStream(minidump), kRunId);
}
#else
// The client's crash reaches the Run's log, as its other lines do, under its
// Run ID, and says where the core is.
// Requirements: NFR-10
TEST(CrashHandlerDeathTest, FatalSignalIsLoggedIntoTheRunsLogFile) {
  const std::filesystem::path log = EmptyDirectory("FatalSignalIsLoggedIntoTheRunsLogFile") / "run.log";

  EXPECT_DEATH(CrashWithStdoutOnStderr("client", {.minidump_file = std::nullopt,
                                                  .run_id = RunId(),
                                                  .log_file = log,
                                                  .core_location = "coredumpctl dump augustac"}),
               "event=crash signal=SIGSEGV");

  const std::vector<std::string> lines = ReadLines(log);
  ASSERT_GE(lines.size(), 2U);
  EXPECT_EQ(lines[0].substr(lines[0].find(' ')),
            " CRITICAL subsystem=client event=crash signal=SIGSEGV core=\"coredumpctl dump augustac\" "
            "run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f");
  EXPECT_TRUE(lines[1].contains(" CRITICAL subsystem=client event=crash_frame index=0 pc=0x")) << lines[1];
  EXPECT_TRUE(lines[1].ends_with(" run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f")) << lines[1];
}
#endif

}  // namespace
