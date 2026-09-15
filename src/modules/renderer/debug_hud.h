#pragma once

#include <Core/API/RasterizerState.h>
#include <Core/Window.h>
#include <Falcor.h>
#include <Utils/Timing/ProfilerUI.h>
#include <Utils/UI/Gui.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace augusta::renderer {

// Falcor's own ImGui wrapper (Gui/ProfilerUI) rather than a bespoke augusta
// overlay - PresentationWorld's real UI doesn't exist yet (ADR-0009's spike
// scope). Owns every ImGui-specific concern (the Gui/ProfilerUI objects,
// theme, window layout, widget wiring) so Renderer::Impl only has to hand
// over the data it displays and the render-state fields its widgets edit -
// it doesn't need to know ImGui exists.
class DebugHud final {
 public:
  DebugHud(const Falcor::ref<Falcor::Device>& device, Falcor::uint2 window_size);

  // Per-frame, read-only Stats-window content - Renderer::Impl already
  // tracks all of this, so DebugHud takes it instead of duplicating
  // storage or reaching back into Renderer::Impl for it.
  struct FrameStats {
    std::string frame_rate_text;
    float cpu_frame_time_ms = 0.0F;
    std::uint64_t frame_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    // Falcor::Gui::render()'s own ImGui delta-time parameter - along for
    // the ride here since every Render() call needs one regardless of
    // what the Stats window shows.
    float delta_time_s = 0.0F;
  };

  // Settings-window fields, edited in place - live Renderer::Impl render
  // state (see renderer.cpp's own "Render settings" comment), not HUD
  // state, so DebugHud only borrows references to it instead of owning a
  // copy. on_rasterizer_state_changed/on_vsync_changed fire when the
  // corresponding widget changes cull_mode/wireframe_enabled or
  // vsync_enabled, so Renderer::Impl can rebuild whatever GPU state
  // depends on them - DebugHud has no reason to know
  // RebuildRasterizerState()/RecreateSwapchain() exist.
  struct RenderSettings {
    Falcor::float4& clear_color;
    Falcor::RasterizerState::CullMode& cull_mode;
    bool& wireframe_enabled;
    bool& vsync_enabled;
    std::function<void()> on_rasterizer_state_changed;
    std::function<void()> on_vsync_changed;
  };

  void Render(Falcor::RenderContext* render_context, const Falcor::ref<Falcor::Fbo>& target_fbo,
              std::string_view gpu_name, std::string_view api_name, Falcor::uint2 resolution, const FrameStats& stats,
              const RenderSettings& settings);

  // Forwarded from Renderer::Impl's Window::ICallbacks so ImGui gets first
  // look at platform input before it reaches gameplay (see renderer.cpp's
  // handleKeyboardEvent/handleMouseEvent) - true means the GUI consumed
  // the event.
  bool OnKeyboardEvent(const Falcor::KeyboardEvent& event);
  bool OnMouseEvent(const Falcor::MouseEvent& event);
  void OnWindowResize(std::uint32_t width, std::uint32_t height);

 private:
  void DrawStatsWindow(const FrameStats& stats);
  void DrawSettingsWindow(std::string_view gpu_name, std::string_view api_name, Falcor::uint2 resolution,
                          const RenderSettings& settings);
  void DrawProfilerWindow();

  Falcor::ref<Falcor::Device> device;
  std::unique_ptr<Falcor::Gui> gui;
  std::unique_ptr<Falcor::ProfilerUI> profiler_ui;
};

}  // namespace augusta::renderer
