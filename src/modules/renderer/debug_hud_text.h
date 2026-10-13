#ifndef AUGUSTA_RENDERER_DEBUG_HUD_TEXT_H_
#define AUGUSTA_RENDERER_DEBUG_HUD_TEXT_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

#include "augusta/renderer.h"

/// \file
/// What the debug HUD reads (ADR-0059), free of any GPU API so it is built and
/// tested on every platform: both Renderers draw this text, so they read the
/// same.
namespace augusta::renderer {

/// The HUD's one line: "FPS: 144 (6.9ms) | RTT: 12ms | Jitter: 0.4ms |
/// Loss: 0.0% | In: 1.2KB/s | Out: 512B/s". No measured frame time yet (zero,
/// negative or non-finite) reads "FPS: --"; not connected reads just
/// "RTT: --"; a connection number not measured yet reads "--".
std::string DebugHudText(double average_frame_time_s, const std::optional<DebugHudNetStats>& net);

/// The average of the last kFrames frame times, so the HUD's FPS doesn't
/// jitter from frame to frame.
class FrameTimeWindow {
 public:
  using Clock = std::chrono::steady_clock;

  /// Falcor::FrameRate's window, which the Falcor HUD reads.
  static constexpr std::size_t kFrames = 60;

  /// Ends a frame at now: the first call only starts the first frame.
  void Tick(Clock::time_point now);

  /// In seconds; zero before the first frame ends.
  [[nodiscard]] double Average() const;

 private:
  std::optional<Clock::time_point> last_tick_;
  std::array<double, kFrames> frame_times_s_{};
  std::size_t count_ = 0;
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_DEBUG_HUD_TEXT_H_
