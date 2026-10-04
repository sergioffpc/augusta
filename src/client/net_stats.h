#ifndef AUGUSTA_CLIENT_NET_STATS_H_
#define AUGUSTA_CLIENT_NET_STATS_H_

#include <chrono>
#include <cstdint>
#include <optional>

#include <nvtx3/nvtx3.hpp>

#include "augusta/networking.h"
#include "augusta/renderer.h"

/// \file
/// What the client makes of its connection's numbers, sampled once per Network
/// I/O round: the readout the renderer's debug HUD shows, and the counters Nsight
/// Systems plots.
namespace augusta::client {

/// Packet loss in percent, from the worse of the two directions; nullopt while
/// neither is measured. Qualities are 0..1 (1 = no loss); negative means not
/// measured yet.
[[nodiscard]] std::optional<float> PacketLossPercent(const networking::ConnectionStats& stats);

/// Turns the connection's numbers into the debug HUD's. ConnectionStats reports
/// jitter as a high-water mark cleared by every read, and the Network I/O thread
/// reads it far faster than anyone can read a HUD, so the HUD shows the peak over
/// the last kJitterWindow.
class HudNetStats {
 public:
  static constexpr std::chrono::seconds kJitterWindow{1};

  /// What the HUD shows after stats, sampled at now; nullopt while there is no
  /// connection to measure, which also starts the jitter window over.
  [[nodiscard]] std::optional<renderer::DebugHudNetStats> Update(
      const std::optional<networking::ConnectionStats>& stats, std::chrono::steady_clock::time_point now);

 private:
  // When the current jitter window started, or nullopt before the first sample.
  std::optional<std::chrono::steady_clock::time_point> jitter_window_start_;
  std::int32_t jitter_window_max_us_ = -1;
  // The last full window's peak jitter; nullopt until one has a measurement.
  std::optional<float> jitter_ms_;
};

/// NVTX counters (nvtx3::counter, third_party/nvtx) mirroring
/// networking::ConnectionStats field-for-field - plotted on the Nsight Systems
/// timeline alongside the Simulation/Network/Render ranges. A sample without
/// stats (not yet kConnected) is recorded as having no value rather than skipped,
/// so the timeline shows an explicit gap rather than a misleading flat line at
/// whatever value came before.
class NetStatsCounters {
 public:
  void Sample(const std::optional<networking::ConnectionStats>& stats);

 private:
  nvtx3::counter<double> ping_ms_{"network.ping_ms", "Round-trip time to server"};
  nvtx3::counter<double> quality_local_{"network.quality_local", "Local packet delivery quality (0-1)"};
  nvtx3::counter<double> quality_remote_{"network.quality_remote", "Remote-reported packet delivery quality (0-1)"};
  nvtx3::counter<double> in_bytes_per_sec_{"network.in_bytes_per_sec", "Inbound throughput"};
  nvtx3::counter<double> out_bytes_per_sec_{"network.out_bytes_per_sec", "Outbound throughput"};
  nvtx3::counter<double> max_jitter_us_{"network.max_jitter_us", "Worst jitter since last GetConnectionStats() call"};
  nvtx3::counter<double> pending_bytes_{"network.pending_bytes", "Bytes queued or in flight"};
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_NET_STATS_H_
