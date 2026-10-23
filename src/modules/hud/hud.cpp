#include "augusta/hud.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <format>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace augusta::hud {

namespace {

std::string FormatFps(double average_frame_time_s) {
  constexpr double kMsPerSecond = 1000.0;
  if (!(average_frame_time_s > 0.0) || !std::isfinite(average_frame_time_s)) {
    return "FPS: --";
  }
  return std::format("FPS: {} ({:.1f}ms)", std::lround(1.0 / average_frame_time_s),
                     average_frame_time_s * kMsPerSecond);
}

std::string FormatNet(const std::optional<NetStats>& net) {
  if (!net.has_value()) {
    return "RTT: --";
  }
  const std::string jitter = net->jitter_ms.has_value() ? std::format("{:.1f}ms", *net->jitter_ms) : "--";
  const std::string loss = net->loss_percent.has_value() ? std::format("{:.1f}%", *net->loss_percent) : "--";
  return std::format("RTT: {}ms | Jitter: {} | Loss: {} | In: {} | Out: {}", net->rtt_ms, jitter, loss,
                     FormatRate(net->in_bytes_per_sec), FormatRate(net->out_bytes_per_sec));
}

}  // namespace

std::string FormatBytes(double bytes) {
  constexpr double kKiB = 1024.0;
  constexpr double kMiB = kKiB * 1024.0;
  if (bytes >= kMiB) {
    return std::format("{:.1f}MB", bytes / kMiB);
  }
  if (bytes >= kKiB) {
    return std::format("{:.1f}KB", bytes / kKiB);
  }
  return std::format("{:.0f}B", bytes);
}

std::string FormatRate(double bytes_per_sec) { return FormatBytes(bytes_per_sec) + "/s"; }

std::vector<std::string> Lines(double average_frame_time_s, const std::optional<NetStats>& net, bool impaired) {
  return {FormatFps(average_frame_time_s) + " | " + FormatNet(net) + (impaired ? " | IMPAIRED" : "")};
}

void FrameTimeWindow::Tick(Clock::time_point now) {
  if (last_tick_.has_value()) {
    frame_times_s_.at(count_ % kFrames) = std::chrono::duration<double>(now - *last_tick_).count();
    ++count_;
  }
  last_tick_ = now;
}

double FrameTimeWindow::Average() const {
  const std::size_t frames = std::min(count_, kFrames);
  if (frames == 0) {
    return 0.0;
  }
  const std::span<const double> recent = std::span(frame_times_s_).first(frames);
  return std::accumulate(recent.begin(), recent.end(), 0.0) / static_cast<double>(frames);
}

}  // namespace augusta::hud
