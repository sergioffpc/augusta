#ifndef AUGUSTA_SERVER_CONNECTION_SAMPLE_H_
#define AUGUSTA_SERVER_CONNECTION_SAMPLE_H_

#include <optional>

#include "augusta/networking.h"
#include "match.h"

/// \file
/// One sample of a client's Connection health, as Host takes it from the
/// transport (Host::SampleConnections) and ConnectionHealth records it
/// (connection_health.h, ADR-0049). A plain struct of its own, so Host need not
/// depend on the metrics that consume it.
namespace augusta::server {

/// One connection's transport measurements, with the Session it carries.
struct ConnectionSample {
  /// nullopt for a connection that has not joined (yet): it counts in the
  /// histograms but has no gauges.
  std::optional<SessionId> session;
  networking::ConnectionStats stats;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_CONNECTION_SAMPLE_H_
