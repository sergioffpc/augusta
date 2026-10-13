#ifndef AUGUSTA_RENDERER_DEBUG_HUD_H_
#define AUGUSTA_RENDERER_DEBUG_HUD_H_

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include "augusta/renderer.h"
#include "debug_hud_text.h"
#include "imgui_renderer.h"

/// \file
/// The Renderer's debug HUD (ADR-0059), private to the module: its line
/// (debug_hud_text.h) as green text in the frame's top left, drawn with Dear
/// ImGui through ImGuiRenderer. System metrics only, never gameplay state. It owns
/// every ImGui concern, so Renderer::Impl only hands it the numbers.
namespace augusta::renderer {

class DebugHud final {
 public:
  /// Installs ImGui's GLFW callbacks on window. They chain to the callbacks
  /// already installed, so make this after the Renderer's own: input still
  /// reaches them. window and device must outlive this HUD.
  DebugHud(GLFWwindow& window, nvrhi::IDevice& device);
  ~DebugHud();

  DebugHud(const DebugHud&) = delete;
  DebugHud& operator=(const DebugHud&) = delete;
  DebugHud(DebugHud&&) = delete;
  DebugHud& operator=(DebugHud&&) = delete;

  /// Records the HUD into commands, over framebuffer's contents. Call once a
  /// frame, after the scene: each call ends a frame for the FPS it reads.
  void Render(nvrhi::ICommandList& commands, nvrhi::IFramebuffer& framebuffer, const DebugHudStats& stats);

 private:
  // Its own, so another ImGui user in the process keeps its state. Made, with
  // the HUD's font, before renderer_ uploads the font.
  ImGuiContext* context_;
  ImGuiRenderer renderer_;
  FrameTimeWindow frame_times_;
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_DEBUG_HUD_H_
