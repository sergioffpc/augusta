#include "augusta/input.h"

namespace augusta::input {

// TODO(sergioffpc): every method below is a placeholder - Input doesn't
// actually accumulate device state yet. Just enough is defined here for
// callers to construct/link against this module.

Input::Input([[maybe_unused]] const Config& config) {
  // Nothing to own yet - no device state has been pushed.
}

Command Input::Sample() {
  // TODO(sergioffpc): build this tick's Command from accumulated device
  // state, then reset per-tick fields (reload edge, yaw/pitch delta).
  return {};
}

void Input::OnKeyEvent([[maybe_unused]] const KeyEvent& event) {
  // TODO(sergioffpc): accumulate key press/release state.
}

void Input::OnMouseButtonEvent([[maybe_unused]] const MouseButtonEvent& event) {
  // TODO(sergioffpc): accumulate mouse button press/release state.
}

void Input::OnMouseMoveEvent([[maybe_unused]] const MouseMoveEvent& event) {
  // TODO(sergioffpc): accumulate yaw/pitch delta from cursor movement.
}

}  // namespace augusta::input
