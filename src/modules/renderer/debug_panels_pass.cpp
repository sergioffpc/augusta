#include "debug_panels_pass.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include "augusta/debug_panels.h"
#include "augusta/window_events.h"

namespace augusta::renderer {

namespace {

// The first panel's top left, under the debug HUD's first lines, and how far
// down and right each next panel ImGui sees opens from the one before it.
constexpr ImVec2 kFirstPanelPos(10.0F, 60.0F);
constexpr float kPanelCascade = 30.0F;

// The time ImGui is told passes between frames: the panels do not animate, and
// a click is a press and a release in two frames, whatever their length.
constexpr float kFrameSeconds = 1.0F / 60.0F;

// Sized to fit their elements every frame, and never folded away to their title.
constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;

// A new ImGui context, current, set up for the panels.
ImGuiContext* CreateContext() {
  ImGuiContext* context = ImGui::CreateContext();
  ImGui::SetCurrentContext(context);
  ImGuiIO& io = ImGui::GetIO();
  // No imgui.ini beside the executable, so panels open at their default
  // positions every run, and the cursor's shape and lock stay the window's.
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  io.Fonts->AddFontDefault();
  return context;
}

// The default top left of the panel ImGui saw slot-th, from 0.
ImVec2 DefaultPosition(std::size_t slot) {
  const float offset = kPanelCascade * static_cast<float>(slot);
  return {kFirstPanelPos.x + offset, kFirstPanelPos.y + offset};
}

// The ImGui mouse button key is, or nullopt if it is a key.
std::optional<int> ImGuiMouseButton(window::Key key) {
  switch (key) {
    case window::Key::kMouseLeft:
      return ImGuiMouseButton_Left;
    case window::Key::kMouseRight:
      return ImGuiMouseButton_Right;
    case window::Key::kMouseMiddle:
      return ImGuiMouseButton_Middle;
    default:
      return std::nullopt;
  }
}

// How far key is from first, both of window::Key, as an ImGuiKey from first's.
ImGuiKey Offset(window::Key key, window::Key first, ImGuiKey imgui_first) {
  return static_cast<ImGuiKey>(imgui_first + (static_cast<int>(key) - static_cast<int>(first)));
}

// The ImGui key a keyboard key is.
ImGuiKey ImGuiKeyFor(window::Key key) {
  if (key >= window::Key::kA && key <= window::Key::kZ) {
    return Offset(key, window::Key::kA, ImGuiKey_A);
  }
  if (key >= window::Key::k0 && key <= window::Key::k9) {
    return Offset(key, window::Key::k0, ImGuiKey_0);
  }
  if (key >= window::Key::kF1 && key <= window::Key::kF12) {
    return Offset(key, window::Key::kF1, ImGuiKey_F1);
  }
  switch (key) {
    case window::Key::kSpace:
      return ImGuiKey_Space;
    case window::Key::kTab:
      return ImGuiKey_Tab;
    case window::Key::kEnter:
      return ImGuiKey_Enter;
    case window::Key::kBackspace:
      return ImGuiKey_Backspace;
    case window::Key::kEscape:
      return ImGuiKey_Escape;
    case window::Key::kLeftShift:
      return ImGuiKey_LeftShift;
    case window::Key::kRightShift:
      return ImGuiKey_RightShift;
    case window::Key::kLeftControl:
      return ImGuiKey_LeftCtrl;
    case window::Key::kRightControl:
      return ImGuiKey_RightCtrl;
    case window::Key::kLeftAlt:
      return ImGuiKey_LeftAlt;
    case window::Key::kRightAlt:
      return ImGuiKey_RightAlt;
    case window::Key::kUp:
      return ImGuiKey_UpArrow;
    case window::Key::kDown:
      return ImGuiKey_DownArrow;
    case window::Key::kLeft:
      return ImGuiKey_LeftArrow;
    case window::Key::kRight:
      return ImGuiKey_RightArrow;
    default:
      return ImGuiKey_None;
  }
}

// The modifier a key is part of, which ImGui reads apart from the key itself
// (a ctrl-click), or ImGuiKey_None.
ImGuiKey ImGuiModifierFor(window::Key key) {
  switch (key) {
    case window::Key::kLeftShift:
    case window::Key::kRightShift:
      return ImGuiMod_Shift;
    case window::Key::kLeftControl:
    case window::Key::kRightControl:
      return ImGuiMod_Ctrl;
    case window::Key::kLeftAlt:
    case window::Key::kRightAlt:
      return ImGuiMod_Alt;
    default:
      return ImGuiKey_None;
  }
}

// Feeds one window event to the current context.
struct EventFeeder {
  ImGuiIO& io;

  void operator()(const window::MouseMoveEvent& move) const { io.AddMousePosEvent(move.x, move.y); }

  void operator()(const window::KeyEvent& event) const {
    const bool down = event.state == window::KeyState::kPressed;
    if (const std::optional<int> button = ImGuiMouseButton(event.key)) {
      io.AddMouseButtonEvent(*button, down);
      return;
    }
    if (const ImGuiKey modifier = ImGuiModifierFor(event.key); modifier != ImGuiKey_None) {
      io.AddKeyEvent(modifier, down);
    }
    if (const ImGuiKey key = ImGuiKeyFor(event.key); key != ImGuiKey_None) {
      io.AddKeyEvent(key, down);
    }
  }
};

// The number of columns table needs: its header's, or its widest row's.
int ColumnCount(const debug::Table& table) {
  std::size_t columns = table.header.size();
  for (const std::vector<std::string>& row : table.rows) {
    columns = std::max(columns, row.size());
  }
  return static_cast<int>(columns);
}

// Draws table's header, if it has one, and rows into the current ImGui table,
// of columns columns.
void DrawTable(const debug::Table& table, int columns) {
  if (!table.header.empty()) {
    for (int column = 0; column < columns; ++column) {
      const auto index = static_cast<std::size_t>(column);
      ImGui::TableSetupColumn(index < table.header.size() ? table.header[index].c_str() : "");
    }
    ImGui::TableHeadersRow();
  }
  for (const std::vector<std::string>& row : table.rows) {
    ImGui::TableNextRow();
    for (const std::string& cell : row) {
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(cell.c_str());
    }
  }
}

// A control, drawn by draw: known to ImGui by its id, so it keeps its state
// while elements before it come and go, and greyed out, giving no change, when
// it says so.
template <typename Control, typename Draw>
void DrawControl(const Control& control, Draw draw) {
  ImGui::PushID(control.id.c_str());
  ImGui::BeginDisabled(control.disabled);
  draw();
  ImGui::EndDisabled();
  ImGui::PopID();
}

// Draws one of a panel's elements, the one at place, into the current ImGui
// window, appending to events the change made to it if it is a control.
struct ElementDrawer {
  const std::string& panel;
  std::size_t place;
  std::vector<debug::ControlEvent>& events;

  void operator()(const debug::Text& text) const { ImGui::TextUnformatted(text.text.c_str()); }

  void operator()(const debug::Table& table) const {
    const int columns = ColumnCount(table);
    // Known to ImGui by its place, as it has no id.
    ImGui::PushID(static_cast<int>(place));
    if (columns > 0 && ImGui::BeginTable("table", columns, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
      DrawTable(table, columns);
      ImGui::EndTable();
    }
    ImGui::PopID();
  }

  void operator()(const debug::HistoryPlot& plot) const {
    ImGui::PlotLines(plot.label.c_str(), plot.values.data(), static_cast<int>(plot.values.size()));
  }

  void operator()(const debug::Toggle& toggle) const {
    DrawControl(toggle, [&] {
      bool on = toggle.on;
      if (ImGui::Checkbox(toggle.label.c_str(), &on)) {
        events.push_back({.panel = panel, .control = toggle.id, .value = on});
      }
    });
  }

  void operator()(const debug::Slider& slider) const {
    DrawControl(slider, [&] {
      float value = slider.value;
      // ImGui's own default format, with three decimals, unless whole.
      const char* format = slider.whole_numbers ? "%.0f" : "%.3f";
      if (ImGui::SliderFloat(slider.label.c_str(), &value, slider.min, slider.max, format)) {
        events.push_back({.panel = panel, .control = slider.id, .value = value});
      }
    });
  }

  void operator()(const debug::Button& button) const {
    DrawControl(button, [&] {
      if (ImGui::Button(button.label.c_str())) {
        events.push_back({.panel = panel, .control = button.id, .value = debug::ButtonPress{}});
      }
    });
  }
};

// Draws panel as its own window, its title shown and its id keeping its place,
// opening at the default position of slot, appending to events the changes
// made to its controls.
void DrawPanel(const debug::Panel& panel, std::size_t slot, std::vector<debug::ControlEvent>& events) {
  ImGui::SetNextWindowPos(DefaultPosition(slot), ImGuiCond_FirstUseEver);
  // No p_open, so no close button.
  ImGui::Begin((panel.title + "###" + panel.id).c_str(), nullptr, kPanelFlags);
  for (std::size_t element = 0; element < panel.elements.size(); ++element) {
    std::visit(ElementDrawer{.panel = panel.id, .place = element, .events = events}, panel.elements[element]);
  }
  ImGui::End();
}

}  // namespace

DebugPanelsPass::DebugPanelsPass(nvrhi::IDevice& device) : context_(CreateContext()), renderer_(device) {}

DebugPanelsPass::~DebugPanelsPass() { ImGui::DestroyContext(context_); }

std::size_t DebugPanelsPass::Slot(const std::string& id) {
  const auto seen = std::ranges::find(seen_ids_, id);
  if (seen != seen_ids_.end()) {
    return static_cast<std::size_t>(seen - seen_ids_.begin());
  }
  seen_ids_.push_back(id);
  return seen_ids_.size() - 1;
}

void DebugPanelsPass::Handle(std::span<const window::Event> events) {
  ImGui::SetCurrentContext(context_);
  const EventFeeder feeder{.io = ImGui::GetIO()};
  for (const window::Event& event : events) {
    std::visit(feeder, event);
  }
}

std::vector<debug::ControlEvent> DebugPanelsPass::Render(nvrhi::ICommandList& commands,
                                                         nvrhi::IFramebuffer& framebuffer,
                                                         const debug::Panels& panels) {
  if (panels.empty()) {
    return {};
  }
  ImGui::SetCurrentContext(context_);
  ImGuiIO& io = ImGui::GetIO();
  const nvrhi::FramebufferInfoEx& frame = framebuffer.getFramebufferInfo();
  io.DisplaySize = ImVec2(static_cast<float>(frame.width), static_cast<float>(frame.height));
  io.DeltaTime = kFrameSeconds;
  ImGui::NewFrame();
  std::vector<debug::ControlEvent> events;
  for (const debug::Panel& panel : panels) {
    DrawPanel(panel, Slot(panel.id), events);
  }
  ImGui::Render();
  renderer_.Render(commands, framebuffer, *ImGui::GetDrawData());
  return events;
}

}  // namespace augusta::renderer
