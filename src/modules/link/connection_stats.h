#ifndef AUGUSTA_LINK_CONNECTION_STATS_H_
#define AUGUSTA_LINK_CONNECTION_STATS_H_

#include "augusta/link.h"
#include "augusta/networking.h"

/// \file
/// The transport's numbers for a connection in the Link's own type, private to
/// the module so it is tested apart from a live connection.
namespace augusta::link {

/// stats, field for field, sentinels and all.
constexpr ConnectionStats FromTransport(const networking::ConnectionStats& stats) {
  return ConnectionStats{
      .ping_ms = stats.ping_ms,
      .quality_local = stats.quality_local,
      .quality_remote = stats.quality_remote,
      .in_bytes_per_sec = stats.in_bytes_per_sec,
      .out_bytes_per_sec = stats.out_bytes_per_sec,
      .max_jitter_us = stats.max_jitter_us,
      .pending_bytes = stats.pending_bytes,
  };
}

}  // namespace augusta::link

#endif  // AUGUSTA_LINK_CONNECTION_STATS_H_
