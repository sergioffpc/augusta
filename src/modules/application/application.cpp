#include "augusta/application.h"

#include <string_view>

#include "augusta/failure.h"
#include "augusta/logging.h"

#ifdef _WIN32
#include <windows.h>
// timeapi.h needs the types windows.h declares, so it comes after it, unsorted.
#include <timeapi.h>
#endif

namespace augusta::application {

#ifdef _WIN32
namespace {

constexpr UINT kTimerPeriodMs = 1;

}  // namespace

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

int Conclude(std::string_view executable, const Outcome& outcome) {
  if (!outcome.has_value()) {
    LOG_INFO("subsystem={} event=exiting exit_status={}", executable, kStoppedExitStatus);
    return kStoppedExitStatus;
  }
  LOG_CRITICAL("subsystem={} event=terminal_failure {} exit_status={}", executable, failure::DescribeFailure(*outcome),
               kFailedExitStatus);
  return kFailedExitStatus;
}

}  // namespace augusta::application
