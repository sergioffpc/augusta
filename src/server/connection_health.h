#ifndef AUGUSTA_SERVER_CONNECTION_HEALTH_H_
#define AUGUSTA_SERVER_CONNECTION_HEALTH_H_

#include <memory>
#include <vector>

#include <prometheus/collectable.h>
#include <prometheus/metric_family.h>

#include "connection_sample.h"

/// \file
/// Every client's Connection health as augustad's metrics report it (ADR-0049,
/// "Catalogue"): histograms over every connection, for the trend and the
/// alerts, and gauges labelled by Session ID, for the one player whose
/// connection is bad. The transport measures and Host says which Session each
/// connection carries (Host::SampleConnections); ServerRuntime hands each
/// sample here once a heartbeat interval on the Network I/O thread (ADR-0005,
/// ADR-0029), and the metrics endpoint (metrics.h) collects it on its own
/// thread. Recording never waits on a collection: the histograms are lock-free
/// (lock_free_metrics.h), and every Session's gauges are published together,
/// once a Record, into a fixed set of slots, one per player a Lobby can hold.
/// No address or Character name is ever a label.
namespace augusta::server {

/// Written by one thread and collected by another.
class ConnectionHealth final : public prometheus::Collectable {
 public:
  ConnectionHealth();
  ~ConnectionHealth() override;

  /// Not copyable or movable: the endpoint collects it in place.
  ConnectionHealth(const ConnectionHealth&) = delete;
  ConnectionHealth& operator=(const ConnectionHealth&) = delete;
  ConnectionHealth(ConnectionHealth&&) = delete;
  ConnectionHealth& operator=(ConnectionHealth&&) = delete;

  /// Records one sample of every open connection. A value the transport has
  /// not measured yet (a negative quality or jitter) is not recorded, and a
  /// Session's gauge keeps its last measured value meanwhile. A Session missing
  /// from samples has ended, and its gauges are removed. From one thread (the
  /// Network I/O thread); never waits on Collect.
  void Record(const std::vector<ConnectionSample>& samples);

  /// The augustad_connection_* and augustad_session_connection_* families,
  /// every Session's gauges as one Record left them. From any thread.
  [[nodiscard]] std::vector<prometheus::MetricFamily> Collect() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_CONNECTION_HEALTH_H_
