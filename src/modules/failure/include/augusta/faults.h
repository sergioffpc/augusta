#ifndef AUGUSTA_FAULTS_H_
#define AUGUSTA_FAULTS_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

/// \file
/// Controlled fault injection: a test arms a Site, and the next call into the
/// dependency there fails as if the dependency had, so a runtime-boundary test
/// can watch cleanup, termination, degradation and isolation happen (ADR-0033).
/// A runtime takes a Faults it asks at each Site and
/// converts a trip into the dependency's own way of failing; nothing arms one
/// outside a test, so in production every Site stays quiet. Any thread may
/// Trip; arming is the test's. failure.h is the model a trip ends up classified
/// in.
namespace augusta::failure {

/// Where a dependency can be made to fail. kMetricsAccept stays last: Faults
/// sizes its table by it.
enum class Site : std::uint8_t {
  kDependencyInit,
  kListenerSetup,
  kWorkerCreation,
  kWorkerExecution,
  kTransportSend,
  kTransportReceive,
  kRecordingWrite,
  kRecordingFlush,
  kCaptureWrite,
  kMetricsAccept,
};

/// What ThrowIfTripped throws: a dependency's exception, standing in for the
/// one the real dependency would throw.
class InjectedFault : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// The armed Sites of one runtime, or of one test.
class Faults {
 public:
  /// Passed as Arm's times, the Site trips until it is disarmed.
  static constexpr std::uint32_t kEveryTime = std::numeric_limits<std::uint32_t>::max();

  /// Makes the next times Trips at site fail with detail, the dependency's
  /// message; replaces whatever site was armed with.
  void Arm(Site site, std::string_view detail, std::uint32_t times = 1);

  /// Makes site stop failing.
  void Disarm(Site site);

  /// The detail of a fault armed at site, taking one of its times, or nullopt
  /// when none is: the call site then fails the way its dependency does. One
  /// relaxed load when nothing is armed, so cheap on a hot path.
  [[nodiscard]] std::optional<std::string> Trip(Site site);

  /// Throws InjectedFault with the detail when Trip(site) trips, for a call
  /// site whose dependency fails by throwing.
  void ThrowIfTripped(Site site);

 private:
  static constexpr std::size_t kSiteCount = static_cast<std::size_t>(Site::kMetricsAccept) + 1;

  struct Armed {
    std::atomic<std::uint32_t> times{0};
    std::string detail;  // Guarded by mutex_.
  };

  std::mutex mutex_;
  std::array<Armed, kSiteCount> sites_{};
};

}  // namespace augusta::failure

#endif  // AUGUSTA_FAULTS_H_
