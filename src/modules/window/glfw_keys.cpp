#include "glfw_keys.h"

#include <optional>

#include <GLFW/glfw3.h>

#include "augusta/window_events.h"

namespace augusta::window {

namespace {

constexpr int kLetterCount = 26;
constexpr int kDigitCount = 10;
constexpr int kFunctionKeyCount = 12;

// GLFW's key in the run [glfw_first, glfw_first + count) as the key at the same
// offset from first, if it is in the run. Both keep letters, digits and F1-F12
// contiguous and in the same order.
std::optional<Key> MapRun(int glfw_key, int glfw_first, Key first, int count) {
  const int offset = glfw_key - glfw_first;
  if (offset < 0 || offset >= count) {
    return std::nullopt;
  }
  return static_cast<Key>(static_cast<int>(first) + offset);
}

std::optional<Key> MapNamedKey(int glfw_key) {
  switch (glfw_key) {
    case GLFW_KEY_SPACE:
      return Key::kSpace;
    case GLFW_KEY_TAB:
      return Key::kTab;
    case GLFW_KEY_ENTER:
      return Key::kEnter;
    case GLFW_KEY_BACKSPACE:
      return Key::kBackspace;
    case GLFW_KEY_ESCAPE:
      return Key::kEscape;
    case GLFW_KEY_LEFT_SHIFT:
      return Key::kLeftShift;
    case GLFW_KEY_RIGHT_SHIFT:
      return Key::kRightShift;
    case GLFW_KEY_LEFT_CONTROL:
      return Key::kLeftControl;
    case GLFW_KEY_RIGHT_CONTROL:
      return Key::kRightControl;
    case GLFW_KEY_LEFT_ALT:
      return Key::kLeftAlt;
    case GLFW_KEY_RIGHT_ALT:
      return Key::kRightAlt;
    case GLFW_KEY_UP:
      return Key::kUp;
    case GLFW_KEY_DOWN:
      return Key::kDown;
    case GLFW_KEY_LEFT:
      return Key::kLeft;
    case GLFW_KEY_RIGHT:
      return Key::kRight;
    default:
      return std::nullopt;
  }
}

}  // namespace

std::optional<Key> KeyFromGlfw(int glfw_key) {
  if (const auto letter = MapRun(glfw_key, GLFW_KEY_A, Key::kA, kLetterCount)) {
    return letter;
  }
  if (const auto digit = MapRun(glfw_key, GLFW_KEY_0, Key::k0, kDigitCount)) {
    return digit;
  }
  if (const auto function_key = MapRun(glfw_key, GLFW_KEY_F1, Key::kF1, kFunctionKeyCount)) {
    return function_key;
  }
  return MapNamedKey(glfw_key);
}

std::optional<Key> MouseButtonFromGlfw(int glfw_button) {
  switch (glfw_button) {
    case GLFW_MOUSE_BUTTON_LEFT:
      return Key::kMouseLeft;
    case GLFW_MOUSE_BUTTON_RIGHT:
      return Key::kMouseRight;
    case GLFW_MOUSE_BUTTON_MIDDLE:
      return Key::kMouseMiddle;
    default:
      return std::nullopt;
  }
}

std::optional<KeyState> KeyStateFromGlfw(int glfw_action) {
  switch (glfw_action) {
    case GLFW_PRESS:
      return KeyState::kPressed;
    case GLFW_RELEASE:
      return KeyState::kReleased;
    default:
      return std::nullopt;
  }
}

}  // namespace augusta::window
