#ifndef AUGUSTA_SERVER_METRICS_H_
#define AUGUSTA_SERVER_METRICS_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include "augusta/tick.h"

/// \file
/// augustad's metrics endpoint (ADR-0049): an HTTP server on one TCP port
/// serving /metrics, the Prometheus text exposition the cluster's Prometheus
/// scrapes, and /livez, the Deployment's liveness probe (liveness.h decides
/// it). ServerRuntime owns it. It runs on civetweb's threads (a listener and
/// one worker that answers requests), together the server's third beside
/// Network I/O and Simulation (ADR-0005), and only reads: the Simulation
/// thread writes what it reports. The endpoint is not a
/// supervised worker, since an HTTP request must never stop the tick loop: a
/// request that fails is logged and answered 500.
namespace augusta::server {

/// The server's metrics endpoint, serving from construction to destruction.
class MetricsEndpoint {
 public:
  /// Starts serving on port, on every interface. last_tick_end is when the
  /// Simulation thread last finished a tick, which /livez reads; it must
  /// outlive this. augustad_build_info's commit comes from the AUGUSTA_COMMIT
  /// environment variable the server image sets, "unknown" without it, and
  /// augustad_start_time_seconds is now. Throws std::runtime_error if the port
  /// can't be bound.
  MetricsEndpoint(std::uint16_t port, const std::atomic<tick::Clock::time_point>& last_tick_end);

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
