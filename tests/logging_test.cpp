#include "augusta/logging.h"

#include <chrono>
#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace {

using augusta::logging::FormatLine;
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
