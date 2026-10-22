#include "augusta/logging.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

namespace {

using augusta::logging::ClearContext;
using augusta::logging::ContextField;
using augusta::logging::FormatLine;
using augusta::logging::ParseSeverity;
using augusta::logging::SetContext;
using augusta::logging::SetLogLevel;
using augusta::logging::Severity;
using augusta::logging::Throttle;
using augusta::logging::WithSuppressed;

const std::chrono::steady_clock::time_point kStart{std::chrono::seconds{100}};

// 2024-02-01T12:00:00Z
const std::chrono::system_clock::time_point kFixedTime{std::chrono::seconds{1706788800}};

TEST(LoggingFormat, LineIsUtcTimestampLevelThenMessage) {
  EXPECT_EQ(FormatLine(kFixedTime, Severity::kInfo, "subsystem=client event=starting", false),
            "2024-02-01T12:00:00Z INFO subsystem=client event=starting");
}

TEST(LoggingFormat, LevelIsUpperCase) {
  EXPECT_EQ(FormatLine(kFixedTime, Severity::kTrace, "m", false), "2024-02-01T12:00:00Z TRACE m");
  EXPECT_EQ(FormatLine(kFixedTime, Severity::kDebug, "m", false), "2024-02-01T12:00:00Z DEBUG m");
  EXPECT_EQ(FormatLine(kFixedTime, Severity::kWarn, "m", false), "2024-02-01T12:00:00Z WARN m");
  EXPECT_EQ(FormatLine(kFixedTime, Severity::kError, "m", false), "2024-02-01T12:00:00Z ERROR m");
  EXPECT_EQ(FormatLine(kFixedTime, Severity::kCritical, "m", false), "2024-02-01T12:00:00Z CRITICAL m");
}

TEST(LoggingFormat, SubSecondTimeIsDropped) {
  EXPECT_EQ(FormatLine(kFixedTime + std::chrono::milliseconds{999}, Severity::kInfo, "m", false),
            "2024-02-01T12:00:00Z INFO m");
}

TEST(LoggingFormat, ColorWrapsOnlyTheLevel) {
  EXPECT_EQ(FormatLine(kFixedTime, Severity::kInfo, "m", true), "2024-02-01T12:00:00Z \x1b[32mINFO\x1b[m m");
  EXPECT_EQ(FormatLine(kFixedTime, Severity::kError, "m", true), "2024-02-01T12:00:00Z \x1b[31m\x1b[1mERROR\x1b[m m");
}

TEST(LoggingInit, CanBeCalledTwiceAndLoggedThrough) {
  augusta::logging::Init();
  augusta::logging::Init();

  LOG_INFO("subsystem=test event=logged value={}", 42);
  SUCCEED();
}

// Whether written is the one console line FormatLine gives message at kInfo, at a
// time from before to after (whole seconds, so either end), colored or not -
// Init chose that by whether stdout was a terminal.
bool IsInfoLineWrittenBetween(const std::string& written, std::string_view message,
                              std::chrono::system_clock::time_point before,
                              std::chrono::system_clock::time_point after) {
  for (const auto time : {before, after}) {
    for (const bool colored : {false, true}) {
      if (written == FormatLine(time, Severity::kInfo, message, colored) + "\n") {
        return true;
      }
    }
  }
  return false;
}

TEST(LoggingRuntimeLevel, CallsUnderItDoNotEvaluateTheirArguments) {
  SetLogLevel(Severity::kInfo);
  int evaluated = 0;
  [[maybe_unused]] const auto side_effect = [&evaluated] { return ++evaluated; };

  LOG_TRACE("subsystem=test event=dropped value={}", side_effect());
  LOG_DEBUG("subsystem=test event=dropped value={}", side_effect());

  EXPECT_EQ(evaluated, 0);
}

TEST(LoggingRuntimeLevel, CallsAtOrAboveItAreWrittenInTheLineFormat) {
  augusta::logging::Init();
  SetLogLevel(Severity::kInfo);

  const auto before = std::chrono::system_clock::now();
  testing::internal::CaptureStdout();
  LOG_INFO("subsystem=test event=written value={}", 7);
  const std::string written = testing::internal::GetCapturedStdout();
  const auto after = std::chrono::system_clock::now();

  EXPECT_TRUE(IsInfoLineWrittenBetween(written, "subsystem=test event=written value=7", before, after)) << written;
}

// What one LOG_INFO of message writes to the console, with no context left
// over from an earlier test unless the test sets it.
std::string WrittenInfoLine(std::string_view message) {
  augusta::logging::Init();
  SetLogLevel(Severity::kInfo);
  testing::internal::CaptureStdout();
  LOG_INFO("{}", message);
  return testing::internal::GetCapturedStdout();
}

void ClearEveryContextField() {
  ClearContext(ContextField::kRun);
  ClearContext(ContextField::kSession);
  ClearContext(ContextField::kMatch);
}

TEST(LoggingContext, ALineWrittenWithNoContextCarriesNoField) {
  ClearEveryContextField();

  const auto before = std::chrono::system_clock::now();
  const std::string written = WrittenInfoLine("subsystem=test event=plain");
  const auto after = std::chrono::system_clock::now();

  EXPECT_TRUE(IsInfoLineWrittenBetween(written, "subsystem=test event=plain", before, after)) << written;
}

TEST(LoggingContext, ALineCarriesTheRunOnceItIsSet) {
  ClearEveryContextField();
  SetContext(ContextField::kRun, "017f22e2-79b0-7cc3-98c4-dc0c0c07398f");

  const auto before = std::chrono::system_clock::now();
  const std::string written = WrittenInfoLine("subsystem=test event=run");
  const auto after = std::chrono::system_clock::now();
  ClearEveryContextField();

  EXPECT_TRUE(IsInfoLineWrittenBetween(written, "subsystem=test event=run run=017f22e2-79b0-7cc3-98c4-dc0c0c07398f",
                                       before, after))
      << written;
}

TEST(LoggingContext, SessionAndMatchFollowTheRunWhileSetAndAreGoneOnceCleared) {
  ClearEveryContextField();
  SetContext(ContextField::kMatch, "m1");
  SetContext(ContextField::kSession, "s1");
  SetContext(ContextField::kRun, "r1");

  auto before = std::chrono::system_clock::now();
  const std::string during = WrittenInfoLine("subsystem=test event=match");
  auto after = std::chrono::system_clock::now();
  EXPECT_TRUE(IsInfoLineWrittenBetween(during, "subsystem=test event=match run=r1 session=s1 match=m1", before, after))
      << during;

  ClearContext(ContextField::kMatch);
  ClearContext(ContextField::kSession);
  before = std::chrono::system_clock::now();
  const std::string cleared = WrittenInfoLine("subsystem=test event=match_end");
  after = std::chrono::system_clock::now();
  ClearEveryContextField();

  EXPECT_TRUE(IsInfoLineWrittenBetween(cleared, "subsystem=test event=match_end run=r1", before, after)) << cleared;
}

TEST(LoggingContext, SettingAFieldAgainReplacesIt) {
  ClearEveryContextField();
  SetContext(ContextField::kSession, "s1");
  SetContext(ContextField::kSession, "s2");

  const auto before = std::chrono::system_clock::now();
  const std::string written = WrittenInfoLine("subsystem=test event=again");
  const auto after = std::chrono::system_clock::now();
  ClearEveryContextField();

  EXPECT_TRUE(IsInfoLineWrittenBetween(written, "subsystem=test event=again session=s2", before, after)) << written;
}

std::string ReadWhole(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

TEST(LoggingFileSink, ALineReachesTheFileAsItIsWrittenAndUncolored) {
  ClearEveryContextField();
  const std::filesystem::path file =
      std::filesystem::path(testing::TempDir()) / "augusta_logging_test" / "logs" / "file_sink.log";
  std::filesystem::remove(file);
  augusta::logging::Init();
  ASSERT_TRUE(augusta::logging::AddFileSink(file).has_value());
  SetContext(ContextField::kRun, "r1");

  const auto before = std::chrono::system_clock::now();
  const std::string console = WrittenInfoLine("subsystem=test event=to_file");
  const auto after = std::chrono::system_clock::now();
  ClearEveryContextField();

  // Read while the sink still holds the file: what was written is flushed.
  const std::string in_file = ReadWhole(file);
  bool matched = false;
  for (const auto time : {before, after}) {
    matched =
        matched || in_file == FormatLine(time, Severity::kInfo, "subsystem=test event=to_file run=r1", false) + "\n";
  }
  EXPECT_TRUE(matched) << in_file;
  EXPECT_TRUE(IsInfoLineWrittenBetween(console, "subsystem=test event=to_file run=r1", before, after)) << console;
}

// Before it is removed a line reaches the file; after, none does, and the file
// is closed, so it can be deleted.
TEST(LoggingFileSink, ALineAfterTheSinkIsRemovedReachesTheFileNoMore) {
  ClearEveryContextField();
  const std::filesystem::path file =
      std::filesystem::path(testing::TempDir()) / "augusta_logging_test" / "logs" / "removed_sink.log";
  std::filesystem::remove(file);
  augusta::logging::Init();
  ASSERT_TRUE(augusta::logging::AddFileSink(file).has_value());
  WrittenInfoLine("subsystem=test event=before_removal");

  augusta::logging::RemoveFileSink(file);
  WrittenInfoLine("subsystem=test event=after_removal");

  const std::string in_file = ReadWhole(file);
  EXPECT_NE(in_file.find("event=before_removal"), std::string::npos) << in_file;
  EXPECT_EQ(in_file.find("event=after_removal"), std::string::npos) << in_file;
  std::error_code error;
  EXPECT_TRUE(std::filesystem::remove(file, error)) << error.message();
}

TEST(LoggingFileSink, AFileThatCannotBeOpenedIsAnError) {
  // A directory where the file would be.
  const std::filesystem::path directory = std::filesystem::path(testing::TempDir()) / "augusta_logging_test_dir";
  std::filesystem::create_directories(directory);

  const auto added = augusta::logging::AddFileSink(directory);

  ASSERT_FALSE(added.has_value());
  EXPECT_EQ(added.error().step, augusta::logging::FileSinkError::Step::kOpen);
  EXPECT_EQ(added.error().path, directory);
}

TEST(LoggingFileSink, AFileWhoseDirectoryCannotBeMadeIsAnErrorNamingTheDirectory) {
  // A file where the directory would be.
  const std::filesystem::path root = std::filesystem::path(testing::TempDir()) / "augusta_logging_test_file_parent";
  std::filesystem::create_directories(root);
  const std::filesystem::path in_the_way = root / "not_a_directory";
  std::ofstream(in_the_way, std::ios::binary) << "x";

  const auto added = augusta::logging::AddFileSink(in_the_way / "logs" / "run.log");

  ASSERT_FALSE(added.has_value());
  EXPECT_EQ(added.error().step, augusta::logging::FileSinkError::Step::kCreateDirectory);
  EXPECT_EQ(added.error().path, in_the_way / "logs");
  EXPECT_TRUE(added.error().reason);
  EXPECT_TRUE(augusta::logging::DescribeFileSinkError(added.error()).starts_with("cannot create "))
      << added.error().path;
}

TEST(LoggingRuntimeLevel, LimitedWarningUnderItTakesNoThrottleSlot) {
  SetLogLevel(Severity::kError);
  Throttle throttle{std::chrono::hours{1}};
  int evaluated = 0;

  LOG_WARN_LIMITED(throttle, "subsystem=test event=dropped value={}", ++evaluated);

  EXPECT_EQ(evaluated, 0);
  EXPECT_EQ(throttle.Admit(std::chrono::steady_clock::now()), 0U);
}

TEST(LoggingRuntimeLevel, EveryLevelsLimitedVariantUnderItTakesNoThrottleSlot) {
  SetLogLevel(Severity::kCritical);
  Throttle throttle{std::chrono::hours{1}};
  int evaluated = 0;

  LOG_TRACE_LIMITED(throttle, "subsystem=test event=dropped value={}", ++evaluated);
  LOG_DEBUG_LIMITED(throttle, "subsystem=test event=dropped value={}", ++evaluated);
  LOG_INFO_LIMITED(throttle, "subsystem=test event=dropped value={}", ++evaluated);
  LOG_ERROR_LIMITED(throttle, "subsystem=test event=dropped value={}", ++evaluated);

  EXPECT_EQ(evaluated, 0);
  EXPECT_EQ(throttle.Admit(std::chrono::steady_clock::now()), 0U);
}

TEST(LoggingRuntimeLevel, ALimitedLineIsWrittenOnceAnInterval) {
  augusta::logging::Init();
  SetLogLevel(Severity::kInfo);
  Throttle throttle{std::chrono::hours{1}};

  const auto before = std::chrono::system_clock::now();
  testing::internal::CaptureStdout();
  for (int i = 0; i < 3; ++i) {
    LOG_INFO_LIMITED(throttle, "subsystem=test event=limited value={}", i);
  }
  const std::string written = testing::internal::GetCapturedStdout();
  const auto after = std::chrono::system_clock::now();

  EXPECT_TRUE(IsInfoLineWrittenBetween(written, "subsystem=test event=limited value=0", before, after)) << written;
  EXPECT_EQ(throttle.Admit(std::chrono::steady_clock::now()), std::nullopt);
}

TEST(LoggingParseSeverity, ParsesEachName) {
  EXPECT_EQ(ParseSeverity("trace"), Severity::kTrace);
  EXPECT_EQ(ParseSeverity("debug"), Severity::kDebug);
  EXPECT_EQ(ParseSeverity("info"), Severity::kInfo);
  EXPECT_EQ(ParseSeverity("warn"), Severity::kWarn);
  EXPECT_EQ(ParseSeverity("error"), Severity::kError);
  EXPECT_EQ(ParseSeverity("critical"), Severity::kCritical);
}

TEST(LoggingParseSeverity, RejectsAnythingElse) {
  EXPECT_EQ(ParseSeverity("TRACE"), std::nullopt);
  EXPECT_EQ(ParseSeverity("verbose"), std::nullopt);
  EXPECT_EQ(ParseSeverity(""), std::nullopt);
}

TEST(LoggingThrottle, FirstEventIsLetThroughWithNothingSuppressed) {
  Throttle throttle{std::chrono::seconds{1}};

  EXPECT_EQ(throttle.Admit(kStart), 0U);
}

TEST(LoggingThrottle, EventsWithinTheIntervalAreTurnedAway) {
  Throttle throttle{std::chrono::seconds{1}};
  throttle.Admit(kStart);

  EXPECT_EQ(throttle.Admit(kStart + std::chrono::milliseconds{1}), std::nullopt);
  EXPECT_EQ(throttle.Admit(kStart + std::chrono::milliseconds{999}), std::nullopt);
}

TEST(LoggingThrottle, NextEventAfterTheIntervalReportsHowManyWereTurnedAway) {
  Throttle throttle{std::chrono::seconds{1}};
  throttle.Admit(kStart);
  throttle.Admit(kStart + std::chrono::milliseconds{100});
  throttle.Admit(kStart + std::chrono::milliseconds{200});

  EXPECT_EQ(throttle.Admit(kStart + std::chrono::seconds{1}), 2U);
}

TEST(LoggingThrottle, CountStartsOverAfterAnEventIsLetThrough) {
  Throttle throttle{std::chrono::seconds{1}};
  throttle.Admit(kStart);
  throttle.Admit(kStart + std::chrono::milliseconds{100});
  throttle.Admit(kStart + std::chrono::seconds{1});

  EXPECT_EQ(throttle.Admit(kStart + std::chrono::seconds{2}), 0U);
}

TEST(LoggingThrottle, IntervalRunsFromTheLastEventLetThrough) {
  Throttle throttle{std::chrono::seconds{1}};
  throttle.Admit(kStart);
  throttle.Admit(kStart + std::chrono::milliseconds{1500});

  EXPECT_EQ(throttle.Admit(kStart + std::chrono::milliseconds{2000}), std::nullopt);
}

TEST(LoggingThrottle, MessageNamesTheSuppressedCountOnlyWhenThereWasOne) {
  EXPECT_EQ(WithSuppressed("event=dropped", 0), "event=dropped");
  EXPECT_EQ(WithSuppressed("event=dropped", 7), "event=dropped suppressed=7");
}

}  // namespace
