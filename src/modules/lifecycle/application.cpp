#include "augusta/application.h"

#include <string_view>

#include "augusta/failure.h"
#include "augusta/logging.h"

namespace augusta::application {

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
