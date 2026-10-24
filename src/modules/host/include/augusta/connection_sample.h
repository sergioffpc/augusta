#ifndef AUGUSTA_CONNECTION_SAMPLE_H_
#define AUGUSTA_CONNECTION_SAMPLE_H_

#include <optional>

#include "augusta/link.h"
#include "augusta/match.h"

/// \file
/// One sample of a client's Connection health, as Host takes it from the Link
/// (Host::SampleConnections) and ConnectionHealth records it
/// (metrics_connection_health.h, ADR-0049). A plain struct of its own, so Host need not
/// depend on the metrics that consume it.
namespace augusta::host {

/// One connection's numbers, as the Link reads them, with the Session it carries.
struct ConnectionSample {
  /// nullopt for a connection that has not joined (yet): it counts in the
  /// histograms but has no gauges.
  std::optional<SessionId> session;
  link::ConnectionStats stats;
};

}  // namespace augusta::host

#endif  // AUGUSTA_CONNECTION_SAMPLE_H_
