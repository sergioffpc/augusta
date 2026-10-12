#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "augusta/logging.h"
#include "crash.h"

namespace {

// 9999-12-31T23:59:59Z, the last second a four-digit year can name, or the last
// one the log sink's system_clock holds if that ends sooner (2262 where it
// counts nanoseconds, as libstdc++'s does).
constexpr std::int64_t kLastSecond = std::min<std::int64_t>(
    253402300799,
    std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::time_point::max()).time_since_epoch().count());

// Any time from the epoch to kLastSecond is written as the log sink's
// FormatLine writes it, leap days and all, though the crash lines can't use its
// std::format.
// Requirements: NFR-10
RC_GTEST_PROP(CrashFormatProperty, TimestampMatchesTheLogSinks, ()) {
  const std::chrono::sys_seconds time{std::chrono::seconds{*rc::gen::inRange<std::int64_t>(0, kLastSecond + 1)}};
  augusta::server::CrashLine line{};
  const std::string message = "subsystem=server event=crash signal=SIGSEGV";
  RC_ASSERT(std::string(augusta::server::FormatCrashSignalLine(line, time, SIGSEGV)) ==
            augusta::logging::FormatLine(time, augusta::logging::Severity::kCritical, message, false) + "\n");
}

}  // namespace
