#ifndef AUGUSTA_RENDERER_DEBUG_HUD_H_
#define AUGUSTA_RENDERER_DEBUG_HUD_H_

#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include "imgui_renderer.h"

/// \file
/// The Renderer's debug HUD (ADR-0059), private to the module: the text lines
/// it is handed (augusta::hud's) as green text in the frame's top left, drawn
/// with Dear ImGui through ImGuiRenderer. It owns every ImGui concern, so
/// Renderer::Impl only hands it the lines.
namespace augusta::renderer {

class DebugHud final {
 public:
  /// Installs ImGui's GLFW callbacks on window. They chain to the callbacks
  /// already installed, so make this after the window's own: its events still
  /// reach them. window and device must outlive this HUD.
  DebugHud(GLFWwindow& window, nvrhi::IDevice& device);
  ~DebugHud();

  DebugHud(const DebugHud&) = delete;
  DebugHud& operator=(const DebugHud&) = delete;
  DebugHud(DebugHud&&) = delete;
  DebugHud& operator=(DebugHud&&) = delete;

  /// Records lines into commands, one under the other, over framebuffer's
  /// contents. Call once a frame, after the scene.
  void Render(nvrhi::ICommandList& commands, nvrhi::IFramebuffer& framebuffer, const std::vector<std::string>& lines);

 private:
  // Its own, so another ImGui user in the process keeps its state. Made, with
  // the HUD's font, before renderer_ uploads the font.
  ImGuiContext* context_;
  ImGuiRenderer renderer_;
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_DEBUG_HUD_H_
