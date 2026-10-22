#ifndef AUGUSTA_DEBUG_PANELS_H_
#define AUGUSTA_DEBUG_PANELS_H_

#include <string>
#include <variant>
#include <vector>

/// \file
/// augusta::debug's Debug panels (ADR-0075): what an area's debug tool shows
/// and lets a developer operate, as values, and the control events that come
/// back. An area's module builds its Panel from its state and updates its state
/// from a ControlEvent, both pure; the client Runtime hands a frame's Panels to
/// the Renderer (Renderer::SetDebugPanels) and routes the events it hands back
/// (Renderer::TakeDebugControlEvents) to the area whose panel id each names.
/// The Renderer draws panels through Dear ImGui, which no area sees. Free of
/// any GPU API, of ImGui and of every area module, and compiled only in a
/// Development build (ADR-0074).
namespace augusta::debug {

/// Shown as it is, unwrapped and unformatted, on a line of its own.
struct Text {
  std::string text;
};

/// Rows of text under a header, a cell per column: a row with fewer cells than
/// the header leaves the rest empty.
struct Table {
  std::vector<std::string> header;
  std::vector<std::vector<std::string>> rows;
};

/// A line plot of values, oldest first, which the area keeps from frame to
/// frame: the panel only shows them.
struct HistoryPlot {
  std::string label;
  std::vector<float> values;
};

/// A switch, on or off. Its id is unique in its panel, as every control's is,
/// and names it in the ControlEvent a change gives.
struct Toggle {
  std::string id;
  std::string label;
  bool on = false;
  /// Greyed out: shown, but gives no event, as every control's.
  bool disabled = false;
};

/// A float the developer drags between min and max.
struct Slider {
  std::string id;
  std::string label;
  float value = 0.0F;
  float min = 0.0F;
  float max = 1.0F;
  /// Shown without a fraction, for a count: the value a drag gives may still
  /// have one, which the area rounds.
  bool whole_numbers = false;
  bool disabled = false;
};

struct Button {
  std::string id;
  std::string label;
  bool disabled = false;
};

/// One thing a panel shows, in the order it is shown.
using Element = std::variant<Text, Table, HistoryPlot, Toggle, Slider, Button>;

/// One Debug panel, a pure function of its area's state.
struct Panel {
  /// The area's, unique among a frame's panels: what its ControlEvents name,
  /// and what keeps its place on screen from frame to frame.
  std::string id;
  std::string title;
  std::vector<Element> elements;
};

/// A frame's panels, in the order they open on screen.
using Panels = std::vector<Panel>;

/// A Button's press, which carries no value.
struct ButtonPress {
  bool operator==(const ButtonPress&) const = default;
};

/// What a control changed to: a Toggle's new state, a Slider's new value, or a
/// Button's press.
using ControlValue = std::variant<bool, float, ButtonPress>;

/// A developer's change to one control of one panel.
struct ControlEvent {
  /// The id of the panel the control is in: the area the event is routed to.
  std::string panel;
  /// The control's id, in that panel.
  std::string control;
  ControlValue value;

  bool operator==(const ControlEvent&) const = default;
};

}  // namespace augusta::debug

#endif  // AUGUSTA_DEBUG_PANELS_H_
