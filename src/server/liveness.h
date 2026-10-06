#ifndef AUGUSTA_SERVER_LIVENESS_H_
#define AUGUSTA_SERVER_LIVENESS_H_

#include <chrono>

#include "augusta/tick.h"

/// \file
/// Whether the server's tick loop is still running, the answer the metrics
/// endpoint's /livez gives (metrics.h): Kubernetes restarts a server whose
/// Simulation thread has hung (ADR-0049). Pure (no I/O, no clock of its own: the
/// caller hands it the time), so it is tested without HTTP.
namespace augusta::server {

/// How long the Simulation thread may go without finishing a tick and still be
/// live: far longer than any tick, even a resynchronised one (ADR-0005).
inline constexpr std::chrono::seconds kLivenessWindow{5};

/// Whether a Simulation thread that last finished a tick at last_tick_end is
/// live at now: it finished one within kLivenessWindow. A last_tick_end after
/// now (read after it) is live.
[[nodiscard]] bool IsLive(tick::Clock::time_point last_tick_end, tick::Clock::time_point now);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_LIVENESS_H_
