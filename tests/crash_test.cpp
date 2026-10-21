#include "augusta/crash.h"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/logging.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

using augusta::crash::FormatFrameLine;
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
TEST(CrashFormat, ClientLinesNameTheCoreAndTheRun) {
  Line line{};
  constexpr std::string_view kRun = "0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f";
  EXPECT_EQ(FormatSignalLine(line, "client", kFixedTime, SIGSEGV, "coredumpctl dump augustac", kRun),
            LoggedLine(kFixedTime,
                       "subsystem=client event=crash signal=SIGSEGV core=\"coredumpctl dump augustac\" "
                       "run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f"));
  EXPECT_EQ(FormatFrameLine(line, "client", kFixedTime, 2, 0x30U, "main", kRun),
            LoggedLine(kFixedTime,
                       "subsystem=client event=crash_frame index=2 pc=0x30 symbol=\"main\" "
                       "run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f"));
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
  const std::string_view written =
      FormatFrameLine(line, "client", kFixedTime, 0, 0x10U, symbol.c_str(), "0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f");
  EXPECT_EQ(written.size(), line.size());
  EXPECT_TRUE(written.ends_with("xx\" run=0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f\n"));
}

// The handler writes where the log sink does, stdout; a death test reads
// stderr, so the dying child points one at the other first. Installed as
// subsystem with settings.
void CrashWithStdoutOnStderr(std::string_view subsystem = kServer, augusta::crash::Settings settings = {}) {
#ifdef _WIN32
  SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE));
#else
  dup2(STDERR_FILENO, STDOUT_FILENO);
#endif
  augusta::crash::Install("augusta_crash_tests", subsystem, std::move(settings));
  std::raise(SIGSEGV);
}

// A Run's ID, in its canonical form.
constexpr std::string_view kRunId = "0190f2a4-1b2c-7d3e-8f40-5a6b7c8d9e0f";

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
  EXPECT_DEATH(CrashWithStdoutOnStderr(), "CRITICAL subsystem=server event=crash signal=SIGSEGV");
}

// Requirements: NFR-10
TEST(CrashHandlerDeathTest, FatalSignalLogsTheStack) {
  EXPECT_DEATH(CrashWithStdoutOnStderr(), "CRITICAL subsystem=server event=crash_frame index=0 pc=0x");
}

// The client's crash reaches the Run's log, as its other lines do, under its
// Run ID, and says where the core is. The file is the test's own, which the
// child that dies opens again (on Windows it reruns the test).
// Requirements: NFR-10
TEST(CrashHandlerDeathTest, FatalSignalIsLoggedIntoTheRunsLogFile) {
  const std::filesystem::path log =
      std::filesystem::temp_directory_path() / "augusta_crash_tests" / "FatalSignalIsLoggedIntoTheRunsLogFile.log";
  std::filesystem::create_directories(log.parent_path());
  std::filesystem::remove(log);

  EXPECT_DEATH(CrashWithStdoutOnStderr("client", {.dumps_directory = std::nullopt,
                                                  .run_id = std::string(kRunId),
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

}  // namespace
