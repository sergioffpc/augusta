#include "debug_hud.h"

// Falcor.dll exports imgui's own symbols, so a direct <imgui.h> include links
// fine against its import lib (same as Falcor's own ProfilerUI.cpp). Used for
// the two things Falcor::Gui's own wrappers don't expose: text color and a
// see-through, borderless window.
#include <cmath>
#include <format>
#include <string>

#include <imgui.h>

namespace augusta::renderer {

namespace {

// Frame time in seconds -> "FPS: 144 (6.9ms)". No measured frame time yet
// (zero, negative or non-finite) reads "FPS: --" rather than a bogus number.
std::string FormatFps(double average_frame_time_s) {
  if (!(average_frame_time_s > 0.0) || !std::isfinite(average_frame_time_s)) {
    return "FPS: --";
  }
  return std::format("FPS: {} ({:.1f}ms)", std::lround(1.0 / average_frame_time_s), average_frame_time_s * 1000.0);
}

// Bytes per second -> "512B/s", "1.2KB/s" or "3.4MB/s".
std::string FormatRate(float bytes_per_sec) {
  constexpr float kKiB = 1024.0F;
  constexpr float kMiB = kKiB * 1024.0F;
  if (bytes_per_sec >= kMiB) {
    return std::format("{:.1f}MB/s", bytes_per_sec / kMiB);
  }
  if (bytes_per_sec >= kKiB) {
    return std::format("{:.1f}KB/s", bytes_per_sec / kKiB);
  }
  return std::format("{:.0f}B/s", bytes_per_sec);
}

// The connection part of the line: "RTT: 12ms | Jitter: 0.4ms | Loss: 0.0% |
// In: 1.2KB/s | Out: 512B/s". Not connected reads just "RTT: --"; a value not
// measured yet reads "--".
std::string FormatNet(const std::optional<DebugHudNetStats>& net) {
  if (!net.has_value()) {
    return "RTT: --";
  }
  const std::string jitter = net->jitter_ms.has_value() ? std::format("{:.1f}ms", *net->jitter_ms) : "--";
  const std::string loss = net->loss_percent.has_value() ? std::format("{:.1f}%", *net->loss_percent) : "--";
  return std::format("RTT: {}ms | Jitter: {} | Loss: {} | In: {} | Out: {}", net->rtt_ms, jitter, loss,
                     FormatRate(net->in_bytes_per_sec), FormatRate(net->out_bytes_per_sec));
}

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
    window.text(FormatFps(stats.average_frame_time_s) + " | " + FormatNet(stats.net));
    ImGui::PopStyleColor();
  }
  gui_->render(render_context, target_fbo, stats.delta_time_s);
}

void DebugHud::OnWindowResize(std::uint32_t width, std::uint32_t height) { gui_->onWindowResize(width, height); }

}  // namespace augusta::renderer
