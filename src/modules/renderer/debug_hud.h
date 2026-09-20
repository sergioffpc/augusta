#ifndef AUGUSTA_RENDERER_DEBUG_HUD_H_
#define AUGUSTA_RENDERER_DEBUG_HUD_H_

#include <cstdint>
#include <memory>
#include <optional>

#include <Falcor.h>
#include <Utils/UI/Gui.h>

#include "augusta/renderer.h"

namespace augusta::renderer {

// A small always-on debug readout (frame time, connection quality) drawn as green text over the
// frame, through Falcor's own ImGui wrapper (Falcor::Gui). Much smaller than
// the earlier in-app HUD (Stats/Settings/Profiler windows) - profiling and
// render-state inspection stay with Nsight/Tracy; this is just the numbers
// worth seeing at a glance. Owns every ImGui-specific concern so
// Renderer::Impl only hands over the numbers.
class DebugHud final {
 public:
  struct Stats {
    // Seconds per frame, averaged over Falcor::FrameRate's window.
    double average_frame_time_s = 0.0;
    // Connection numbers, if connected.
    std::optional<DebugHudNetStats> net;
    // Seconds since the previous frame (Falcor::Gui::render's own ImGui
    // delta-time parameter).
    float delta_time_s = 0.0F;
  };

  DebugHud(const Falcor::ref<Falcor::Device>& device, Falcor::uint2 window_size);

  // Draws the HUD into target_fbo. Call after the scene is drawn.
  void Render(Falcor::RenderContext* render_context, const Falcor::ref<Falcor::Fbo>& target_fbo, const Stats& stats);

  void OnWindowResize(std::uint32_t width, std::uint32_t height);

 private:
  std::unique_ptr<Falcor::Gui> gui_;
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_DEBUG_HUD_H_
