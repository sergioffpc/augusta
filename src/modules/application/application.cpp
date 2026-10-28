#include "augusta/application.h"

#include <expected>
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

// Why mimalloc does not serve the process, for DescribeMimallocError's line.
std::string_view MimallocReason(MimallocError error) {
  switch (error) {
    case MimallocError::kNotRedirected:
      return "malloc is not redirected to it (mimalloc.dll must be the first DLL it loads, with "
             "mimalloc-redirect.dll beside it)";
    case MimallocError::kBlockOutsideHeap:
      return "a block malloc handed out is outside mimalloc's heap";
  }
  return "unknown reason";
}

// Where the player gets the latest Visual C++ Redistributable, x64: Microsoft's
// permalink, which always serves the newest one.
constexpr std::string_view kMsvcRedistUrl = "https://aka.ms/vc14/vc_redist.x64.exe";

// version as Windows writes a file version, e.g. "14.44.35211.0".
std::string Dotted(const FileVersion& version) {
  return std::format("{}.{}.{}.{}", version.major, version.minor, version.build, version.revision);
}

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

std::expected<void, MimallocError> CheckMimalloc(const MimallocFacts& facts) {
  if (!facts.redirected) {
    return std::unexpected(MimallocError::kNotRedirected);
  }
  if (!facts.block_in_heap) {
    return std::unexpected(MimallocError::kBlockOutsideHeap);
  }
  return {};
}

std::string DescribeMimallocError(std::string_view executable, MimallocError error) {
  return std::format("{}: mimalloc does not serve this process: {}", executable, MimallocReason(error));
}

std::expected<void, MsvcRuntimeError> CheckMsvcRuntime(const MsvcRuntimeFacts& facts) {
  if (!facts.loaded) {
    return std::unexpected(MsvcRuntimeError::kUnreadable);
  }
  if (*facts.loaded < facts.minimum) {
    return std::unexpected(MsvcRuntimeError::kTooOld);
  }
  return {};
}

std::string DescribeMsvcRuntimeError(std::string_view executable, const MsvcRuntimeFacts& facts,
                                     MsvcRuntimeError error) {
  const std::string reason =
      error == MsvcRuntimeError::kTooOld && facts.loaded
          ? std::format("the Visual C++ runtime it loaded (msvcp140.dll {}) is older than the {} it needs",
                        Dotted(*facts.loaded), Dotted(facts.minimum))
          : std::string("the version of the Visual C++ runtime it loaded (msvcp140.dll) cannot be read");
  return std::format("{}: {}: install the Visual C++ Redistributable (x64) {} or newer, from {}", executable, reason,
                     Dotted(facts.minimum), kMsvcRedistUrl);
}

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
