#include "debug_hud.h"

// Falcor.dll exports imgui's own symbols (Source/Falcor/CMakeLists.txt sets
// IMGUI_API=dllexport for its own build only), so a direct <imgui.h>
// include here - same as Falcor's own ProfilerUI.cpp does - links fine
// against that import lib. Used below to theme the HUD beyond what Gui's
// own widget wrappers expose (ApplyHudTheme).
#include <imgui.h>

#include <cfloat>
#include <cstdint>

namespace augusta::renderer {

namespace {

// Layout for the debug GUI windows (ADR-0009's spike scope: Falcor's own
// ImGui wrapper, not a bespoke augusta HUD - PresentationWorld's real UI
// doesn't exist yet).
// Y offsets are hand-measured (ImGui::GetWindowSize() logged at runtime),
// not computed - AutoResize means the real heights below (Stats 122px,
// Settings 182px, at time of measurement) only change if a window's own
// content (line count) changes, so a fixed-position layout has to be
// re-measured then, same as this fix did (Stats grew past Settings' old
// y=90 once the CPU/Memory/geometry lines were added). ~20px margin below
// each measured height.
constexpr Falcor::uint2 kStatsWindowPos(10, 10);
constexpr Falcor::uint2 kSettingsWindowPos(10, 150);
constexpr Falcor::uint2 kProfilerWindowPos(10, 350);
// Applied to all 3 windows (ImGui::SetNextWindowSizeConstraints, right
// before each one's Begin() below) so they line up at a consistent width
// instead of each auto-shrinking to its own narrowest content.
constexpr float kMinWindowWidth = 700.0F;

// All 3 windows auto-resize to fit their content (no fixed size hint) -
// Gui::WindowFlags::AutoResize maps to ImGuiWindowFlags_AlwaysAutoResize,
// recomputed every frame, so this also tracks content that changes size
// at runtime (e.g. the Profiler window's event table).
// Not constexpr: FALCOR_ENUM_CLASS_OPERATORS' operator| isn't declared
// constexpr, so this has to be a plain const initialized at namespace
// scope (runs once, before main, same as any other global with a
// non-constant initializer).
const Falcor::Gui::WindowFlags kAutoResizeWindowFlags =
    Falcor::Gui::WindowFlags::Default | Falcor::Gui::WindowFlags::AutoResize;

// Monochrome HUD theme - black/white/grey only, no color. Falcor's own
// ImGui wrapper only exposes a handful of style knobs (Gui.cpp's own
// constructor tweaks 3-4 colors the same way); everything else here goes
// through <imgui.h> directly. Note: Falcor::ProfilerUI (the Profiler
// window's contents) hardcodes its own bar/graph colors
// (Source/Falcor/Utils/Timing/ProfilerUI.cpp's kColorPalette etc.) rather
// than reading the ImGui style, so this theme can't reach those - only a
// patch to that vendored file could, which is out of scope for a
// colors-only pass (cmake/patches/ only carries the CMakePresets tweak
// today).
constexpr ImVec4 kAccentPrimary(1.0F, 1.0F, 1.0F, 1.0F);       // white
constexpr ImVec4 kAccentSecondary(0.65F, 0.65F, 0.65F, 1.0F);  // grey

// Neutral grey steps for widget backgrounds (idle -> hovered -> active),
// named so the repeated identical r=g=b literals below don't read as
// unexplained magic numbers.
constexpr float kGreyStepIdle = 0.16F;
constexpr float kGreyStepHovered = 0.20F;
constexpr float kGreyStepActive = 0.26F;
constexpr float kGreyStepScrollbarGrab = 0.24F;

void ApplyHudTheme() {
  ImGuiStyle& style = ImGui::GetStyle();

  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09F, 0.10F, 0.11F, 0.92F);
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0.09F, 0.10F, 0.11F, 0.0F);
  style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08F, 0.09F, 0.10F, 0.96F);
  style.Colors[ImGuiCol_Border] = ImVec4(0.20F, 0.22F, 0.24F, 0.7F);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.11F, 0.13F, 0.14F, 1.0F);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(kGreyStepIdle, kGreyStepIdle, kGreyStepIdle, 1.0F);
  style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09F, 0.10F, 0.11F, 0.8F);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.13F, 0.14F, 0.15F, 1.0F);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(kGreyStepHovered, kGreyStepHovered, kGreyStepHovered, 1.0F);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(kGreyStepActive, kGreyStepActive, kGreyStepActive, 1.0F);
  style.Colors[ImGuiCol_CheckMark] = kAccentPrimary;
  style.Colors[ImGuiCol_SliderGrab] = kAccentSecondary;
  style.Colors[ImGuiCol_SliderGrabActive] = kAccentPrimary;
  style.Colors[ImGuiCol_Button] = ImVec4(kGreyStepIdle, kGreyStepIdle, kGreyStepIdle, 1.0F);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(kAccentSecondary.x, kAccentSecondary.y, kAccentSecondary.z, 0.35F);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.55F);
  style.Colors[ImGuiCol_Header] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.20F);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.35F);
  style.Colors[ImGuiCol_HeaderActive] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.50F);
  style.Colors[ImGuiCol_ResizeGrip] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.25F);
  style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.55F);
  style.Colors[ImGuiCol_ResizeGripActive] = kAccentPrimary;
  style.Colors[ImGuiCol_Separator] = ImVec4(0.20F, 0.22F, 0.24F, 0.7F);
  style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(kAccentSecondary.x, kAccentSecondary.y, kAccentSecondary.z, 0.6F);
  style.Colors[ImGuiCol_SeparatorActive] = kAccentPrimary;
  style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08F, 0.09F, 0.10F, 0.6F);
  style.Colors[ImGuiCol_ScrollbarGrab] =
      ImVec4(kGreyStepScrollbarGrab, kGreyStepScrollbarGrab, kGreyStepScrollbarGrab, 1.0F);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] =
      ImVec4(kAccentSecondary.x, kAccentSecondary.y, kAccentSecondary.z, 0.5F);
  style.Colors[ImGuiCol_ScrollbarGrabActive] = kAccentPrimary;
  style.Colors[ImGuiCol_PlotLines] = kAccentSecondary;
  style.Colors[ImGuiCol_PlotLinesHovered] = kAccentPrimary;
  style.Colors[ImGuiCol_PlotHistogram] = kAccentSecondary;
  style.Colors[ImGuiCol_PlotHistogramHovered] = kAccentPrimary;
  style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.30F);
}

const Falcor::Gui::DropdownList kCullModeList = {
    {.value = static_cast<std::uint32_t>(Falcor::RasterizerState::CullMode::None), .label = "None"},
    {.value = static_cast<std::uint32_t>(Falcor::RasterizerState::CullMode::Front), .label = "Front"},
    {.value = static_cast<std::uint32_t>(Falcor::RasterizerState::CullMode::Back), .label = "Back"},
};

constexpr float kAccentStripWidth = 3.0F;

// The mockup's title bars carried a colored left-edge strip
// (`.hud-title::before`) that plain ImGuiCol_Title* slots can't express -
// draw it directly. Must run with the target Gui::Window already open
// (i.e. from inside its scope): Gui::Window's constructor is what calls
// ImGui::Begin, and that's what makes GetWindowPos()/GetWindowDrawList()
// refer to the right window.
void DrawWindowAccentStrip() {
  const ImVec2 window_pos = ImGui::GetWindowPos();
  const float title_height = ImGui::GetFrameHeight();
  ImGui::GetWindowDrawList()->AddRectFilled(window_pos,
                                            ImVec2(window_pos.x + kAccentStripWidth, window_pos.y + title_height),
                                            ImGui::GetColorU32(kAccentPrimary));
}

}  // namespace

DebugHud::DebugHud(const Falcor::ref<Falcor::Device>& device, Falcor::uint2 window_size) : device(device) {
  // Profiler window's open/close state IS the profiler's enabled flag
  // (see DrawProfilerWindow) - starts open/enabled by default.
  device->getProfiler()->setEnabled(true);

  gui = std::make_unique<Falcor::Gui>(device, window_size.x, window_size.y);
  // Gui's constructor leaves its freshly created ImGuiContext current
  // (Utils/UI/Gui.cpp's GuiImpl ctor: CreateContext + SetCurrentContext,
  // never reset before returning) - same context ApplyHudTheme's
  // ImGui::GetStyle() call below ends up touching.
  ApplyHudTheme();
  // Falcor already bundles a monospace face (data/framework/fonts/
  // consolab.ttf) and loads it under the name "monospace" in Gui's own
  // constructor - reuse that instead of shipping a font of our own, to
  // match the technical/data-readout look from the mockup.
  gui->setActiveFont("monospace");
}

void DebugHud::DrawStatsWindow(const FrameStats& stats) {
  ImGui::SetNextWindowSizeConstraints(ImVec2(kMinWindowWidth, 0.0F), ImVec2(FLT_MAX, FLT_MAX));
  Falcor::Gui::Window stats_window(gui.get(), "Stats", Falcor::uint2(0, 0), kStatsWindowPos, kAutoResizeWindowFlags);
  DrawWindowAccentStrip();
  // frame_rate_text (Falcor::to_string(FrameRate)) reports
  // getAverageFrameTime(), smoothed over the last 60 frames - kept as the
  // stable "true" FPS reading. CPU below is single-frame and unsmoothed,
  // and only the Clear+Cube command-recording portion of the frame (see
  // Renderer::Impl::last_cpu_frame_time_ms) - not comparable to the FPS
  // reading above, which covers the whole paced frame.
  stats_window.text(stats.frame_rate_text);
  stats_window.text(fmt::format("CPU: {:.2f} ms", stats.cpu_frame_time_ms));
  stats_window.text(fmt::format("Frame #{}", stats.frame_count));
  // getCurrentRSS(): resident/working set size for this process
  // (Core/Platform/OS.h) - actual RAM in use, not committed/virtual size,
  // and not GPU memory (Falcor exposes no VRAM query).
  constexpr double kBytesPerMebibyte = 1024.0 * 1024.0;
  stats_window.text(fmt::format("Memory: {:.1f} MB", static_cast<double>(Falcor::getCurrentRSS()) / kBytesPerMebibyte));
  // Falcor exposes no GPU-side pipeline-statistics query (no
  // vertices/primitives-submitted counter to read back), so this is what
  // Renderer::Impl already knows about what it's submitting - exact today
  // because there's exactly one draw call in the scene.
  stats_window.text(fmt::format("Vertices: {} | Triangles: {} | Draws: 1", stats.vertex_count, stats.index_count / 3));
}

void DebugHud::DrawSettingsWindow(std::string_view gpu_name, std::string_view api_name, Falcor::uint2 resolution,
                                  const RenderSettings& settings) {
  ImGui::SetNextWindowSizeConstraints(ImVec2(kMinWindowWidth, 0.0F), ImVec2(FLT_MAX, FLT_MAX));
  Falcor::Gui::Window settings_window(gui.get(), "Settings", Falcor::uint2(0, 0), kSettingsWindowPos,
                                      kAutoResizeWindowFlags);
  DrawWindowAccentStrip();
  settings_window.text(fmt::format("GPU: {}", gpu_name));
  settings_window.text(fmt::format("API: {}", api_name));
  settings_window.text(fmt::format("Resolution: {}x{}", resolution.x, resolution.y));

  settings_window.rgbaColor("Clear color", settings.clear_color);

  auto cull_mode_value = static_cast<std::uint32_t>(settings.cull_mode);
  if (settings_window.dropdown("Cull mode", kCullModeList, cull_mode_value)) {
    settings.cull_mode = static_cast<Falcor::RasterizerState::CullMode>(cull_mode_value);
    settings.on_rasterizer_state_changed();
  }
  if (settings_window.checkbox("Wireframe", settings.wireframe_enabled)) {
    settings.on_rasterizer_state_changed();
  }
  if (settings_window.checkbox("VSync", settings.vsync_enabled)) {
    settings.on_vsync_changed();
  }
}

// Falcor's own ImGui wrapper (Gui/ProfilerUI) rather than a bespoke
// augusta overlay - see debug_hud.h's own header comment. The profiler
// window mirrors Falcor::SampleApp::renderUI()'s own pattern: its
// open/close state IS the profiler's enabled flag, so closing the window
// also stops the profiler from timing events until it's reopened.
void DebugHud::DrawProfilerWindow() {
  bool profiler_open = device->getProfiler()->isEnabled();
  {
    // AutoResize alone settles on the narrowest width that fits the event
    // table, squeezing the graph (ProfilerUI::renderGraph fills whatever's
    // left of the window's width) down to a sliver - a minimum-width
    // constraint (checked by the next Begin(), i.e. the Window constructor
    // right below) keeps AutoResize for height while guaranteeing the
    // graph real estate.
    ImGui::SetNextWindowSizeConstraints(ImVec2(kMinWindowWidth, 0.0F), ImVec2(FLT_MAX, FLT_MAX));
    Falcor::Gui::Window profiler_window(gui.get(), "Profiler", profiler_open, Falcor::uint2(0, 0), kProfilerWindowPos,
                                        kAutoResizeWindowFlags);
    if (profiler_open) {
      // Unlike Stats/Settings (no close button, so always re-opened), this
      // window's `open` flag is real and persists across frames - only
      // draw the strip while the window is actually pushed
      // (ImGui::Begin/GetWindowDrawList would otherwise run outside a
      // matching Begin/End pair when the window is closed).
      DrawWindowAccentStrip();
      if (!profiler_ui) {
        profiler_ui = std::make_unique<Falcor::ProfilerUI>(device->getProfiler());
      }
      profiler_ui->render();
    }
  }
  device->getProfiler()->setEnabled(profiler_open);
}

void DebugHud::Render(Falcor::RenderContext* render_context, const Falcor::ref<Falcor::Fbo>& target_fbo,
                      std::string_view gpu_name, std::string_view api_name, Falcor::uint2 resolution,
                      const FrameStats& stats, const RenderSettings& settings) {
  gui->beginFrame();
  DrawStatsWindow(stats);
  DrawSettingsWindow(gpu_name, api_name, resolution, settings);
  DrawProfilerWindow();
  gui->render(render_context, target_fbo, stats.delta_time_s);
}

bool DebugHud::OnKeyboardEvent(const Falcor::KeyboardEvent& event) { return gui->onKeyboardEvent(event); }

bool DebugHud::OnMouseEvent(const Falcor::MouseEvent& event) { return gui->onMouseEvent(event); }

void DebugHud::OnWindowResize(std::uint32_t width, std::uint32_t height) { gui->onWindowResize(width, height); }

}  // namespace augusta::renderer
