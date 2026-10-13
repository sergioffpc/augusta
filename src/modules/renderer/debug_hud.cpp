#include "debug_hud.h"

// Falcor.dll exports imgui's own symbols, so a direct <imgui.h> include links
// fine against its import lib (same as Falcor's own ProfilerUI.cpp). Used for
// the two things Falcor::Gui's own wrappers don't expose: text color and a
// see-through, borderless window.
#include <cstdint>
#include <memory>

#include <imgui.h>

#include "augusta/renderer.h"
#include "debug_hud_text.h"

namespace augusta::renderer {

namespace {

constexpr ImVec4 kGreen(0.0F, 1.0F, 0.0F, 1.0F);
constexpr Falcor::uint2 kWindowPos(10, 10);

// Only AutoResize is set: no title bar, close button or focus stealing, and
// sized to fit its text every frame. Not constexpr: FALCOR_ENUM_CLASS_OPERATORS'
// operator| isn't.
const Falcor::Gui::WindowFlags kWindowFlags = Falcor::Gui::WindowFlags::Empty | Falcor::Gui::WindowFlags::AutoResize;

}  // namespace

DebugHud::DebugHud(const Falcor::ref<Falcor::Device>& device, Falcor::uint2 window_size)
    : gui_(std::make_unique<Falcor::Gui>(device, window_size.x, window_size.y)) {
  // Falcor already loads a monospace face under this name in Gui's own
  // constructor - keeps the digits from jittering as they change.
  gui_->setActiveFont("monospace");
}

void DebugHud::Render(Falcor::RenderContext* render_context, const Falcor::ref<Falcor::Fbo>& target_fbo,
                      const Stats& stats) {
  gui_->beginFrame();
  {
    // Gui's constructor leaves its ImGuiContext current, so these ImGui calls
    // apply to the window opened just below: fully transparent and without a
    // border, so only the text shows.
    ImGui::SetNextWindowBgAlpha(0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    Falcor::Gui::Window window(gui_.get(), "Debug", Falcor::uint2(0, 0), kWindowPos, kWindowFlags);
    ImGui::PopStyleVar();
    ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
    window.text(DebugHudText(stats.average_frame_time_s, stats.net));
    ImGui::PopStyleColor();
  }
  gui_->render(render_context, target_fbo, stats.delta_time_s);
}

void DebugHud::OnWindowResize(std::uint32_t width, std::uint32_t height) { gui_->onWindowResize(width, height); }

}  // namespace augusta::renderer
