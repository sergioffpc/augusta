#ifndef AUGUSTA_SERVER_METRICS_H_
#define AUGUSTA_SERVER_METRICS_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <prometheus/collectable.h>

#include "augusta/tick.h"

/// \file
/// augustad's metrics endpoint (ADR-0049): an HTTP server on one TCP port
/// serving /metrics, the Prometheus text exposition the cluster's Prometheus
/// scrapes, and /livez, the Deployment's liveness probe (liveness.h decides
/// it). ServerRuntime owns it. Boost.Beast serves it on a thread of its own,
/// the server's third beside Network I/O and Simulation (ADR-0005), which only
/// reads: the Simulation and Network I/O threads write what it reports. That
/// thread is not a supervised worker, since an HTTP request must never stop
/// the tick loop: a request that fails is warned of and answered 500, and a
/// failure on one connection, or a peer that disconnects, ends only it. A failure to accept is
/// retried a bounded number of times, each wait twice the last; one that
/// persists, or the thread failing, is the endpoint's permanent failure, a
/// subsystem one (ADR-0033): it logs it once, stops serving and closes its
/// port, so /livez fails and the platform restarts the pod (ADR-0049) while the
/// tick loop goes on until then. What the Host counts (host_metrics.h) and
/// every client's Connection health (connection_health.h) it serves beside the
/// Process family, which is its own.
namespace augusta::failure {
class Faults;
}  // namespace augusta::failure

namespace augusta::server {

/// What /metrics serves beside the Process family, in order.
using ServerMetrics = std::vector<std::reference_wrapper<const prometheus::Collectable>>;

/// How many accept failures in a row the endpoint takes, the last included,
/// before it stops serving; a connection accepted in between starts the count
/// over.
inline constexpr int kMetricsAcceptAttempts = 5;

/// How long the endpoint waits after its first accept failure in a row before
/// accepting again; each further wait is twice the one before.
inline constexpr std::chrono::milliseconds kMetricsFirstAcceptRetry{100};

/// How long the endpoint waits before accepting again once accepting has failed
/// failures times in a row (from 1), or nullopt when that is its permanent
/// failure: kMetricsFirstAcceptRetry, doubled for each further failure, until
/// kMetricsAcceptAttempts.
[[nodiscard]] std::optional<std::chrono::milliseconds> AcceptRetryDelay(int failures);

/// The server's metrics endpoint, serving from construction to destruction.
class MetricsEndpoint {
 public:
  /// Starts serving on port, on every interface. last_tick_end is when the
  /// Simulation thread last finished a tick, which /livez reads, and
  /// server_metrics what /metrics serves beside the Process family, collected
  /// on this endpoint's thread; each must outlive this. augustad_build_info's
  /// commit comes from the AUGUSTA_COMMIT environment variable the server image
  /// sets, "unknown" without it, and augustad_start_time_seconds is now.
  /// faults, when given, is asked at failure::Site::kMetricsAccept before each
  /// accept, a trip failing it as the acceptor would; only a test gives one,
  /// and it must outlive this. Throws the dependency's exception if the port
  /// can't be bound, which its caller converts (failure::Guard).
  MetricsEndpoint(std::uint16_t port, const std::atomic<tick::Clock::time_point>& last_tick_end,
                  ServerMetrics server_metrics, failure::Faults* faults = nullptr);

  /// Stops serving, waiting for a request in progress.
  ~MetricsEndpoint();

  /// False once the endpoint has failed permanently and stopped serving: its
  /// port is closed and it never serves again. Any thread.
  [[nodiscard]] bool Available() const;

  /// Non-copyable and not movable: its thread refers to it.
  MetricsEndpoint(const MetricsEndpoint&) = delete;
  MetricsEndpoint& operator=(const MetricsEndpoint&) = delete;
  MetricsEndpoint(MetricsEndpoint&&) = delete;
  MetricsEndpoint& operator=(MetricsEndpoint&&) = delete;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_METRICS_H_
