#include "net_stats.h"

#include <algorithm>

namespace augusta::client {

std::optional<float> PacketLossPercent(const networking::ConnectionStats& stats) {
  std::optional<float> worst_quality;
  for (const float quality : {stats.quality_local, stats.quality_remote}) {
    if (quality >= 0.0F) {
      worst_quality = worst_quality.has_value() ? std::min(*worst_quality, quality) : quality;
    }
  }
  if (!worst_quality.has_value()) {
    return std::nullopt;
  }
  return (1.0F - std::min(*worst_quality, 1.0F)) * 100.0F;
}

std::optional<renderer::DebugHudNetStats> HudNetStats::Update(const std::optional<networking::ConnectionStats>& stats,
                                                              std::chrono::steady_clock::time_point now) {
  if (!stats.has_value()) {
    jitter_window_start_.reset();
    jitter_window_max_us_ = -1;
    jitter_ms_.reset();
    return std::nullopt;
  }
  if (!jitter_window_start_.has_value()) {
    jitter_window_start_ = now;
  }
  jitter_window_max_us_ = std::max(jitter_window_max_us_, stats->max_jitter_us);
  if (now - *jitter_window_start_ >= kJitterWindow) {
    constexpr float kMicrosecondsPerMillisecond = 1000.0F;
    jitter_ms_ = jitter_window_max_us_ >= 0
                     ? std::optional<float>(static_cast<float>(jitter_window_max_us_) / kMicrosecondsPerMillisecond)
                     : std::nullopt;
    jitter_window_max_us_ = -1;
    jitter_window_start_ = now;
  }
  return renderer::DebugHudNetStats{.rtt_ms = stats->ping_ms,
                                    .jitter_ms = jitter_ms_,
                                    .loss_percent = PacketLossPercent(*stats),
                                    .in_bytes_per_sec = stats->in_bytes_per_sec,
                                    .out_bytes_per_sec = stats->out_bytes_per_sec};
}

void NetStatsCounters::Sample(const std::optional<networking::ConnectionStats>& stats) {
  if (!stats.has_value()) {
    ping_ms_.sample_no_value(nvtx3::no_value_reason::unavailable);
    quality_local_.sample_no_value(nvtx3::no_value_reason::unavailable);
    quality_remote_.sample_no_value(nvtx3::no_value_reason::unavailable);
    in_bytes_per_sec_.sample_no_value(nvtx3::no_value_reason::unavailable);
    out_bytes_per_sec_.sample_no_value(nvtx3::no_value_reason::unavailable);
    max_jitter_us_.sample_no_value(nvtx3::no_value_reason::unavailable);
    pending_bytes_.sample_no_value(nvtx3::no_value_reason::unavailable);
    return;
  }
  ping_ms_.sample(static_cast<double>(stats->ping_ms));
  quality_local_.sample(static_cast<double>(stats->quality_local));
  quality_remote_.sample(static_cast<double>(stats->quality_remote));
  in_bytes_per_sec_.sample(static_cast<double>(stats->in_bytes_per_sec));
  out_bytes_per_sec_.sample(static_cast<double>(stats->out_bytes_per_sec));
  max_jitter_us_.sample(static_cast<double>(stats->max_jitter_us));
  pending_bytes_.sample(static_cast<double>(stats->pending_bytes));
}

}  // namespace augusta::client
