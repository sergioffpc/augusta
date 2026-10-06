#ifndef AUGUSTA_SERVER_CONNECTION_HEALTH_H_
#define AUGUSTA_SERVER_CONNECTION_HEALTH_H_

#include <memory>
#include <optional>
#include <vector>

#include "augusta/networking.h"
#include "match.h"

namespace prometheus {
class Registry;
}  // namespace prometheus

/// \file
/// Every client's Connection health as augustad's metrics report it (ADR-0049,
/// "Catalogue"): histograms over every connection, for the trend and the
/// alerts, and gauges labelled by Session ID, for the one player whose
/// connection is bad. The transport measures (networking::Server::GetStats);
/// Host says which Session each connection carries; ServerRuntime samples
/// both once a heartbeat interval on the Network I/O thread (ADR-0005,
/// ADR-0029) and hands the sample here, and the metrics endpoint (metrics.h)
/// serves what this records. No address or Character name is ever a label.
namespace augusta::server {

/// One connection's transport measurements, with the Session it carries.
struct ConnectionSample {
  /// nullopt for a connection that has not joined (yet): it counts in the
  /// histograms but has no gauges.
  std::optional<SessionId> session;
  networking::ConnectionStats stats;
};

/// The Connection health metrics, registered in a registry on construction.
class ConnectionHealth {
 public:
  /// Registers the augustad_connection_* families in registry, which must
  /// outlive this.
  explicit ConnectionHealth(prometheus::Registry& registry);
  ~ConnectionHealth();

  /// Not copyable or movable: holds what it registered.
  ConnectionHealth(const ConnectionHealth&) = delete;
  ConnectionHealth& operator=(const ConnectionHealth&) = delete;
  ConnectionHealth(ConnectionHealth&&) = delete;
  ConnectionHealth& operator=(ConnectionHealth&&) = delete;

  /// Records one sample of every open connection. A value the transport has
  /// not measured yet (a negative quality or jitter) is not recorded. A
  /// Session missing from samples has ended, and its gauges are removed.
  /// From one thread at a time (the Network I/O thread).
  void Record(const std::vector<ConnectionSample>& samples);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_CONNECTION_HEALTH_H_
