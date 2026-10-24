#ifndef AUGUSTA_CLIENT_DEBUG_PROFILING_PANEL_H_
#define AUGUSTA_CLIENT_DEBUG_PROFILING_PANEL_H_

#include <string_view>

#include "augusta/debug_panels.h"
#include "augusta/renderer.h"

/// \file
/// The client's Profiling Debug panel (ADR-0075), private to the client
/// Runtime and compiled only in a Development build (ADR-0074): a window of N
/// frames that Nsight Systems' `--capture-range=nvtx` records alone, and
/// whether Aftermath watches the GPU and where a dump would land. Its state is
/// pure: it says when the capture window opens and closes, and the Runtime
/// opens and closes the NVTX range `augusta.capture` at those points. The
/// Renderer reports Aftermath's status (Renderer::GpuCrashDumps).
namespace augusta::client {

/// The Profiling panel's Panel id, which its ControlEvents name.
inline constexpr std::string_view kProfilingPanelId = "profiling";

/// What an update did to the capture window.
enum class CaptureChange {
  kNone,
  /// The window opens: the frames from the next one on are captured.
  kOpens,
  /// The window closes: the frame just ended was its last.
  kCloses,
};

class ProfilingPanel {
 public:
  static constexpr int kDefaultFrames = 300;
  static constexpr int kMinFrames = 1;
  static constexpr int kMaxFrames = 3600;

  /// gpu_crash_dumps: what the Renderer reports of Aftermath, which does not
  /// change once it is made.
  explicit ProfilingPanel(renderer::GpuCrashDumpStatus gpu_crash_dumps);

  /// The panel: a slider for N and a button opening the window, both greyed
  /// out during a capture, how far through the window the capture is, and
  /// Aftermath's status.
  [[nodiscard]] debug::Panel BuildPanel() const;

  /// The button opens a window of N frames when none is open; the slider sets
  /// N, rounded into [kMinFrames, kMaxFrames], when none is. Any other event,
  /// another panel's included, changes nothing.
  [[nodiscard]] CaptureChange Update(const debug::ControlEvent& event);

  /// One frame has ended: the N-th since the window opened closes it.
  [[nodiscard]] CaptureChange Tick();

 private:
  renderer::GpuCrashDumpStatus gpu_crash_dumps_;
  int frames_ = kDefaultFrames;
  // The frames of the open window that have ended, or -1 while none is open.
  int captured_ = -1;

  [[nodiscard]] bool Capturing() const { return captured_ >= 0; }
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_DEBUG_PROFILING_PANEL_H_
