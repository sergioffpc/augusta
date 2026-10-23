#ifndef AUGUSTA_HUD_H_
#define AUGUSTA_HUD_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

/// \file
/// augusta::hud is what the DebugHud shows (ADR-0059): system metrics - the
/// frame rate and the connection's numbers - as text lines, never gameplay
/// state. Free of any GPU API, so it is built and tested on every platform. The
/// client Runtime measures the frames, sources the connection's numbers from
/// the transport and hands the lines to the Renderer, which only draws them.
/// The client's Network Debug panel shows bytes in the HUD's units.
namespace augusta::hud {

/// The connection's numbers the HUD shows.
struct NetStats {
  /// Round-trip time to the server, in milliseconds.
  int rtt_ms = 0;
  /// Recent worst jitter in milliseconds; nullopt if not measured yet.
  std::optional<float> jitter_ms;
  /// Packet loss in percent (worst of the two directions); nullopt if not
  /// measured yet.
  std::optional<float> loss_percent;
  /// Actual throughput over the connection, in bytes per second.
  float in_bytes_per_sec = 0.0F;
  float out_bytes_per_sec = 0.0F;
};

/// The HUD's lines, today one: "FPS: 144 (6.9ms) | RTT: 12ms | Jitter: 0.4ms |
/// Loss: 0.0% | In: 1.2KB/s | Out: 512B/s", ending in " | IMPAIRED" while
/// impaired, connected or not: whether the developer impairs the link from the
/// Network Debug panel. average_frame_time_s is in seconds
/// (FrameTimeWindow::Average); none measured yet (zero, negative or non-finite)
/// reads "FPS: --". net is nullopt while not connected, which reads just
/// "RTT: --"; a connection number not measured yet reads "--".
[[nodiscard]] std::vector<std::string> Lines(double average_frame_time_s, const std::optional<NetStats>& net,
                                             bool impaired);

/// Bytes as the HUD shows them: "300B", "2.0KB" or "3.4MB", in steps of 1024.
/// The Network Debug panel shows its bytes so too, so the two read alike.
[[nodiscard]] std::string FormatBytes(double bytes);

/// Bytes per second as the HUD shows them: "512B/s", "1.2KB/s" or "3.4MB/s".
[[nodiscard]] std::string FormatRate(double bytes_per_sec);

/// The average of the last kFrames frame times, so the HUD's FPS doesn't
/// jitter from frame to frame.
class FrameTimeWindow {
 public:
  using Clock = std::chrono::steady_clock;

  /// A second's worth at 60 FPS.
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

}  // namespace augusta::hud

#endif  // AUGUSTA_HUD_H_
