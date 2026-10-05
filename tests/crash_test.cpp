#include "crash.h"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "augusta/logging.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

using augusta::server::CrashLine;
using augusta::server::FormatCrashFrameLine;
using augusta::server::FormatCrashSignalLine;

// 2024-02-01T12:00:00Z
constexpr std::int64_t kFixedTime = 1706788800;

// What logging::FormatLine writes for message at time, as the console sink ends
// it: the crash lines must read like every other CRITICAL line (ADR-0029).
std::string LoggedLine(std::int64_t unix_seconds, std::string_view message) {
  const std::chrono::system_clock::time_point time{std::chrono::seconds{unix_seconds}};
  return augusta::logging::FormatLine(time, augusta::logging::Severity::kCritical, message, false) + "\n";
}

TEST(CrashFormat, SignalLineIsACriticalLogLine) {
  CrashLine line{};
  EXPECT_EQ(FormatCrashSignalLine(line, kFixedTime, SIGSEGV),
            LoggedLine(kFixedTime, "subsystem=server event=crash signal=SIGSEGV"));
}

TEST(CrashFormat, SignalWithoutANameIsWrittenAsItsNumber) {
  CrashLine line{};
  EXPECT_EQ(FormatCrashSignalLine(line, kFixedTime, 77),
            LoggedLine(kFixedTime, "subsystem=server event=crash signal=77"));
}

TEST(CrashFormat, FrameLineNamesIndexAddressAndQuotedSymbol) {
  CrashLine line{};
  EXPECT_EQ(FormatCrashFrameLine(line, kFixedTime, 3, 0x7f00deadbeefU, "augusta::server::Host::Step(int)"),
            LoggedLine(kFixedTime,
                       "subsystem=server event=crash_frame index=3 pc=0x7f00deadbeef "
                       "symbol=\"augusta::server::Host::Step(int)\""));
}

TEST(CrashFormat, UnresolvedFrameHasNoSymbol) {
  CrashLine line{};
  EXPECT_EQ(FormatCrashFrameLine(line, kFixedTime, 0, 0x10U, nullptr),
            LoggedLine(kFixedTime, "subsystem=server event=crash_frame index=0 pc=0x10"));
}

TEST(CrashFormat, QuotesAndBackslashesInASymbolAreEscaped) {
  CrashLine line{};
  // The symbol f("\") is written "f(\"\\\")".
  EXPECT_EQ(FormatCrashFrameLine(line, kFixedTime, 0, 0x10U, "f(\"\\\")"),
            LoggedLine(kFixedTime, "subsystem=server event=crash_frame index=0 pc=0x10 symbol=\"f(\\\"\\\\\\\")\""));
}

TEST(CrashFormat, SymbolTooLongForTheLineIsCutButStillClosed) {
  CrashLine line{};
  const std::string symbol(line.size() * 2, 'x');
  const std::string_view written = FormatCrashFrameLine(line, kFixedTime, 0, 0x10U, symbol.c_str());
  EXPECT_EQ(written.size(), line.size());
  EXPECT_TRUE(written.ends_with("xx\"\n"));
}

// Any time from the epoch to the year 9999 is written as FormatLine writes it,
// leap days and all, though the crash lines can't use its std::format.
RC_GTEST_PROP(CrashFormat, TimestampMatchesTheLogSinks, ()) {
  const auto unix_seconds = *rc::gen::inRange<std::int64_t>(0, 253402300799);
  CrashLine line{};
  RC_ASSERT(std::string(FormatCrashSignalLine(line, unix_seconds, SIGSEGV)) ==
            LoggedLine(unix_seconds, "subsystem=server event=crash signal=SIGSEGV"));
}

// The handler writes where the log sink does, stdout; a death test reads
// stderr, so the dying child points one at the other first.
void CrashWithStdoutOnStderr() {
#ifdef _WIN32
  SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE));
#else
  dup2(STDERR_FILENO, STDOUT_FILENO);
#endif
  augusta::server::InstallCrashHandler("augusta_crash_tests");
  std::raise(SIGSEGV);
}

// gtest's regular expressions on Windows have no character classes, hence the
// plain substrings.
TEST(CrashHandlerDeathTest, FatalSignalIsLoggedBeforeTheProcessDies) {
  EXPECT_DEATH(CrashWithStdoutOnStderr(), "CRITICAL subsystem=server event=crash signal=SIGSEGV");
}

TEST(CrashHandlerDeathTest, FatalSignalLogsTheStack) {
  EXPECT_DEATH(CrashWithStdoutOnStderr(), "CRITICAL subsystem=server event=crash_frame index=0 pc=0x");
}

}  // namespace
