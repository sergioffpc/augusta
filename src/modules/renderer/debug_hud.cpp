#include "debug_hud.h"

#include <stdexcept>
#include <string>
#include <vector>

#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include "debug_hud_font.h"

namespace augusta::renderer {

namespace {

// Green text, its window's top left 10 pixels into the frame, at a font size
// it does not scale for the display's DPI.
constexpr ImVec4 kGreen(0.0F, 1.0F, 0.0F, 1.0F);
constexpr ImVec2 kWindowPos(10.0F, 10.0F);
constexpr float kFontSizePixels = 14.0F;

// The time ImGui is told passes between frames. The HUD neither animates nor
// takes input, so any positive time draws it the same.
constexpr float kFrameSeconds = 1.0F / 60.0F;

// No title bar, not movable, no focus stealing, and sized to fit its text
// every frame.
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

// A new ImGui context, current, set up for the HUD. It has no platform
// binding: the HUD takes no input, and Render gives ImGui the frame's size.
ImGuiContext* CreateContext() {
  ImGuiContext* context = ImGui::CreateContext();
  ImGui::SetCurrentContext(context);
  ImGuiIO& io = ImGui::GetIO();
  // No imgui.ini beside the executable, and the cursor's shape and lock stay
  // the window's.
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  AddFont(*io.Fonts);
  return context;
}

}  // namespace

DebugHud::DebugHud(nvrhi::IDevice& device) : context_(CreateContext()), renderer_(device) {}

DebugHud::~DebugHud() { ImGui::DestroyContext(context_); }

void DebugHud::Render(nvrhi::ICommandList& commands, nvrhi::IFramebuffer& framebuffer,
                      const std::vector<std::string>& lines) {
  ImGui::SetCurrentContext(context_);
  ImGuiIO& io = ImGui::GetIO();
  const nvrhi::FramebufferInfoEx& frame = framebuffer.getFramebufferInfo();
  io.DisplaySize = ImVec2(static_cast<float>(frame.width), static_cast<float>(frame.height));
  io.DeltaTime = kFrameSeconds;
  ImGui::NewFrame();
  // Fully transparent and without a border, so only the text shows.
  ImGui::SetNextWindowPos(kWindowPos, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
  ImGui::Begin("Debug", nullptr, kWindowFlags);
  ImGui::PopStyleVar();
  ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
  for (const std::string& line : lines) {
    ImGui::TextUnformatted(line.c_str());
  }
  ImGui::PopStyleColor();
  ImGui::End();
  ImGui::Render();
  renderer_.Render(commands, framebuffer, *ImGui::GetDrawData());
}

}  // namespace augusta::renderer
