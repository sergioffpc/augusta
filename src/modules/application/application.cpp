#include "augusta/application.h"

#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "augusta/failure.h"
#include "augusta/logging.h"

#ifdef _WIN32
#include <windows.h>
// timeapi.h needs the types windows.h declares, so it comes after it, unsorted.
#include <timeapi.h>
#endif

namespace augusta::application {

namespace {

#ifdef _WIN32
constexpr UINT kTimerPeriodMs = 1;
#endif

}  // namespace

#ifdef _WIN32
TimerResolution::TimerResolution() : raised_(timeBeginPeriod(kTimerPeriodMs) == TIMERR_NOERROR) {
  if (!raised_) {
    LOG_WARN("subsystem=application event=timer_resolution_unchanged period_ms={}", kTimerPeriodMs);
  }
}

TimerResolution::~TimerResolution() {
  if (raised_) {
    timeEndPeriod(kTimerPeriodMs);
  }
}
#else
TimerResolution::TimerResolution() = default;

TimerResolution::~TimerResolution() = default;
#endif

int Conclude(std::string_view executable, const Outcome& outcome, const std::optional<std::filesystem::path>& run_log) {
  if (!outcome.has_value()) {
    LOG_INFO("subsystem={} event=exiting exit_status={}", executable, kStoppedExitStatus);
    return kStoppedExitStatus;
  }
  const std::string log = run_log ? std::format(" log=\"{}\"", logging::Utf8(*run_log)) : std::string{};
  LOG_CRITICAL("subsystem={} event={} {} exit_status={}{}", executable, kTerminalFailureEvent,
               failure::DescribeFailure(*outcome), kFailedExitStatus, log);
  return kFailedExitStatus;
}

}  // namespace augusta::application
