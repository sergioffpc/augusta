#ifndef AUGUSTA_WINDOW_EVENTS_H_
#define AUGUSTA_WINDOW_EVENTS_H_

#include <cstddef>
#include <variant>

/// \file
/// The window's device events (ADR-0059): the keys and mouse buttons it names,
/// and what Window::Poll returns of them. Free of GLFW, so a module that only
/// reads the events - input, which turns them into Commands - links this
/// target (augusta_window_events) on every platform, never the window itself.
namespace augusta::window {

/// A physical key or mouse button. Mouse buttons are keys here so a control can
/// be bound to either. The engine's own names, not GLFW's: the window maps
/// GLFW's keys onto these.
enum class Key {
  kA,
  kB,
  kC,
  kD,
  kE,
  kF,
  kG,
  kH,
  kI,
  kJ,
  kK,
  kL,
  kM,
  kN,
  kO,
  kP,
  kQ,
  kR,
  kS,
  kT,
  kU,
  kV,
  kW,
  kX,
  kY,
  kZ,
  k0,
  k1,
  k2,
  k3,
  k4,
  k5,
  k6,
  k7,
  k8,
  k9,
  kSpace,
  kTab,
  kEnter,
  kBackspace,
  kEscape,
  kLeftShift,
  kRightShift,
  kLeftControl,
  kRightControl,
  kLeftAlt,
  kRightAlt,
  kUp,
  kDown,
  kLeft,
  kRight,
  kF1,
  kF2,
  kF3,
  kF4,
  kF5,
  kF6,
  kF7,
  kF8,
  kF9,
  kF10,
  kF11,
  kF12,
  kMouseLeft,
  kMouseRight,
  /// Keep last: kKeyCount counts from it.
  kMouseMiddle,
};

/// How many keys Key names.
inline constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::kMouseMiddle) + 1;

/// Whether key is a mouse button.
[[nodiscard]] bool IsMouseButton(Key key);

/// Whether a key transitioned to pressed or released. Held-down state is not
/// reported: a reader derives it from the Pressed/Released pairs.
enum class KeyState {
  kPressed,
  kReleased,
};

/// One key's (or mouse button's) press/release transition.
struct KeyEvent {
  Key key;
  KeyState state;
};

/// The cursor's position within the client area, in pixels, when it moved.
/// (0, 0) is the top-left corner. An absolute position, not a delta; unbounded
/// while the cursor is locked (Window::SetCursorLocked).
struct MouseMoveEvent {
  float x = 0.0F;
  float y = 0.0F;
};

/// One device event, in the order the window received it.
using Event = std::variant<KeyEvent, MouseMoveEvent>;

}  // namespace augusta::window

#endif  // AUGUSTA_WINDOW_EVENTS_H_
