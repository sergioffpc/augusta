#include "nvrhi_debug_hud.h"

#include <chrono>
#include <stdexcept>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <nvrhi/nvrhi.h>

#include "augusta/renderer.h"
#include "debug_hud_font.h"
#include "debug_hud_text.h"

namespace augusta::renderer {

namespace {

// The Falcor HUD's: green text, its window's top left 10 pixels into the
// frame, and Falcor's font size, which it does not scale for the display's DPI.
constexpr ImVec4 kGreen(0.0F, 1.0F, 0.0F, 1.0F);
constexpr ImVec2 kWindowPos(10.0F, 10.0F);
constexpr float kFontSizePixels = 14.0F;

// What Falcor's Gui::Window opens for the Falcor HUD's flags: no title bar,
// not movable, no focus stealing, and sized to fit its text every frame.
constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;

// Adds the HUD's font, embedded at build time, to atlas.
void AddFont(ImFontAtlas& atlas) {
  ImFontConfig font_config;
  // The atlas reads the font from the executable's data, and must not free it.
  font_config.FontDataOwnedByAtlas = false;
  // ImGui takes void*, but only reads it while FontDataOwnedByAtlas is false.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  if (atlas.AddFontFromMemoryTTF(const_cast<unsigned char*>(kDebugHudFont), sizeof(kDebugHudFont), kFontSizePixels,
                                 &font_config) == nullptr) {
    throw std::runtime_error("Loading the debug HUD's font failed");
  }
}

// A new ImGui context, current, set up for the HUD on window.
ImGuiContext* CreateContext(GLFWwindow& window) {
  ImGuiContext* context = ImGui::CreateContext();
  ImGui::SetCurrentContext(context);
  ImGuiIO& io = ImGui::GetIO();
  // No imgui.ini beside the executable, and the cursor's shape and lock stay
  // the Renderer's.
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  AddFont(*io.Fonts);
  if (!ImGui_ImplGlfw_InitForOther(&window, true)) {
    throw std::runtime_error("Initializing ImGui's GLFW binding failed");
  }
  return context;
}

}  // namespace

NvrhiDebugHud::NvrhiDebugHud(GLFWwindow& window, nvrhi::IDevice& device)
    : context_(CreateContext(window)), renderer_(device) {}

NvrhiDebugHud::~NvrhiDebugHud() {
  ImGui::SetCurrentContext(context_);
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext(context_);
}

void NvrhiDebugHud::Render(nvrhi::ICommandList& commands, nvrhi::IFramebuffer& framebuffer,
                           const DebugHudStats& stats) {
  frame_times_.Tick(std::chrono::steady_clock::now());
  ImGui::SetCurrentContext(context_);
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  // Fully transparent and without a border, so only the text shows.
  ImGui::SetNextWindowPos(kWindowPos, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
  ImGui::Begin("Debug", nullptr, kWindowFlags);
  ImGui::PopStyleVar();
  ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
  ImGui::TextUnformatted(DebugHudText(frame_times_.Average(), stats.net).c_str());
  ImGui::PopStyleColor();
  ImGui::End();
  ImGui::Render();
  renderer_.Render(commands, framebuffer, *ImGui::GetDrawData());
}

}  // namespace augusta::renderer
