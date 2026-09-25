#include "augusta/logging.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using augusta::logging::FormatLine;
using augusta::logging::ParseSeverity;
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

  LI("subsystem=test event=logged value={}", 42);
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

  LT("subsystem=test event=dropped value={}", side_effect());
  LD("subsystem=test event=dropped value={}", side_effect());

  EXPECT_EQ(evaluated, 0);
}

TEST(LoggingRuntimeLevel, CallsAtOrAboveItAreWrittenInTheLineFormat) {
  augusta::logging::Init();
  SetLogLevel(Severity::kInfo);

  const auto before = std::chrono::system_clock::now();
  testing::internal::CaptureStdout();
  LI("subsystem=test event=written value={}", 7);
  const std::string written = testing::internal::GetCapturedStdout();
  const auto after = std::chrono::system_clock::now();

  EXPECT_TRUE(IsInfoLineWrittenBetween(written, "subsystem=test event=written value=7", before, after)) << written;
}

TEST(LoggingRuntimeLevel, LimitedWarningUnderItTakesNoThrottleSlot) {
  SetLogLevel(Severity::kError);
  Throttle throttle{std::chrono::hours{1}};
  int evaluated = 0;

  LW_LIMITED(throttle, "subsystem=test event=dropped value={}", ++evaluated);

  EXPECT_EQ(evaluated, 0);
  EXPECT_EQ(throttle.Admit(std::chrono::steady_clock::now()), 0U);
}

TEST(LoggingRuntimeLevel, EveryLevelsLimitedVariantUnderItTakesNoThrottleSlot) {
  SetLogLevel(Severity::kCritical);
  Throttle throttle{std::chrono::hours{1}};
  int evaluated = 0;

  LT_LIMITED(throttle, "subsystem=test event=dropped value={}", ++evaluated);
  LD_LIMITED(throttle, "subsystem=test event=dropped value={}", ++evaluated);
  LI_LIMITED(throttle, "subsystem=test event=dropped value={}", ++evaluated);
  LE_LIMITED(throttle, "subsystem=test event=dropped value={}", ++evaluated);

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
    LI_LIMITED(throttle, "subsystem=test event=limited value={}", i);
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
