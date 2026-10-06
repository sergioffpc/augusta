#ifndef AUGUSTA_SERVER_METRICS_H_
#define AUGUSTA_SERVER_METRICS_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
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
/// the tick loop: a request that fails is logged and answered 500. What the
/// Host counts (host_metrics.h) and every client's Connection health
/// (connection_health.h) it serves beside the Process family, which is its own.
namespace augusta::server {

/// What /metrics serves beside the Process family, in order.
using ServerMetrics = std::vector<std::reference_wrapper<const prometheus::Collectable>>;

/// The server's metrics endpoint, serving from construction to destruction.
class MetricsEndpoint {
 public:
  /// Starts serving on port, on every interface. last_tick_end is when the
  /// Simulation thread last finished a tick, which /livez reads, and
  /// server_metrics what /metrics serves beside the Process family, collected
  /// on this endpoint's thread; each must outlive this. augustad_build_info's
  /// commit comes from the AUGUSTA_COMMIT environment variable the server image
  /// sets, "unknown" without it, and augustad_start_time_seconds is now. Throws
  /// std::runtime_error if the port can't be bound.
  MetricsEndpoint(std::uint16_t port, const std::atomic<tick::Clock::time_point>& last_tick_end,
                  ServerMetrics server_metrics);

  /// Stops serving, waiting for a request in progress.
  ~MetricsEndpoint();

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
