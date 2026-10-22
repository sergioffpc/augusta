#ifndef AUGUSTA_RENDERER_DEBUG_PANELS_PASS_H_
#define AUGUSTA_RENDERER_DEBUG_PANELS_PASS_H_

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include "augusta/debug_panels.h"
#include "augusta/window_events.h"
#include "imgui_renderer.h"

/// \file
/// The Renderer's Debug panels (ADR-0075), private to the module and compiled
/// in a Development build only (ADR-0074): the panels augusta::debug describes,
/// drawn with Dear ImGui through ImGuiRenderer, and operated by the window's
/// events Renderer::Impl hands it, which it feeds to ImGui itself, as ImGui has
/// no platform binding here (ADR-0059). Each frame's drawing gives back the
/// changes made to the panels' controls as augusta::debug's ControlEvents, so
/// nothing outside it sees ImGui.
namespace augusta::renderer {

class DebugPanelsPass final {
 public:
  /// device must outlive this pass.
  explicit DebugPanelsPass(nvrhi::IDevice& device);
  ~DebugPanelsPass();

  DebugPanelsPass(const DebugPanelsPass&) = delete;
  DebugPanelsPass& operator=(const DebugPanelsPass&) = delete;
  DebugPanelsPass(DebugPanelsPass&&) = delete;
  DebugPanelsPass& operator=(DebugPanelsPass&&) = delete;

  /// Queues events, oldest first, for the panels the next Render draws.
  void Handle(std::span<const window::Event> events);

  /// Records panels into commands over framebuffer's contents, and returns the
  /// changes the queued events made to their controls. Draws nothing, and
  /// returns none, for no panels. Call once a frame, last.
  [[nodiscard]] std::vector<debug::ControlEvent> Render(nvrhi::ICommandList& commands, nvrhi::IFramebuffer& framebuffer,
                                                        const debug::Panels& panels);

 private:
  // Its own, apart from the debug HUD's, so the events fed here reach only the
  // panels. Made, with ImGui's default font, before renderer_ uploads the font.
  ImGuiContext* context_;
  ImGuiRenderer renderer_;
  // The ids of the panels drawn so far, in the order they were first drawn:
  // a panel's place here is its default position's slot, so it never shares
  // one with another, whatever the order of a frame's panels.
  std::vector<std::string> seen_ids_;

  // The slot of the panel with id, the next free one if it is new.
  std::size_t Slot(const std::string& id);
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_DEBUG_PANELS_PASS_H_
