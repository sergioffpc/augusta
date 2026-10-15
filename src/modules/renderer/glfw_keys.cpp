#include "glfw_keys.h"

#include <optional>

#include <GLFW/glfw3.h>

#include "augusta/input.h"

namespace augusta::renderer {

namespace {

constexpr int kLetterCount = 26;
constexpr int kDigitCount = 10;
constexpr int kFunctionKeyCount = 12;

// GLFW's key in the run [glfw_first, glfw_first + count) as the key at the same
// offset from first, if it is in the run. Both keep letters, digits and F1-F12
// contiguous and in the same order.
std::optional<input::Key> MapRun(int glfw_key, int glfw_first, input::Key first, int count) {
  const int offset = glfw_key - glfw_first;
  if (offset < 0 || offset >= count) {
    return std::nullopt;
  }
  return static_cast<input::Key>(static_cast<int>(first) + offset);
}

std::optional<input::Key> MapNamedKey(int glfw_key) {
  switch (glfw_key) {
    case GLFW_KEY_SPACE:
      return input::Key::kSpace;
    case GLFW_KEY_TAB:
      return input::Key::kTab;
    case GLFW_KEY_ENTER:
      return input::Key::kEnter;
    case GLFW_KEY_BACKSPACE:
      return input::Key::kBackspace;
    case GLFW_KEY_ESCAPE:
      return input::Key::kEscape;
    case GLFW_KEY_LEFT_SHIFT:
      return input::Key::kLeftShift;
    case GLFW_KEY_RIGHT_SHIFT:
      return input::Key::kRightShift;
    case GLFW_KEY_LEFT_CONTROL:
      return input::Key::kLeftControl;
    case GLFW_KEY_RIGHT_CONTROL:
      return input::Key::kRightControl;
    case GLFW_KEY_LEFT_ALT:
      return input::Key::kLeftAlt;
    case GLFW_KEY_RIGHT_ALT:
      return input::Key::kRightAlt;
    case GLFW_KEY_UP:
      return input::Key::kUp;
    case GLFW_KEY_DOWN:
      return input::Key::kDown;
    case GLFW_KEY_LEFT:
      return input::Key::kLeft;
    case GLFW_KEY_RIGHT:
      return input::Key::kRight;
    default:
      return std::nullopt;
  }
}

}  // namespace

std::optional<input::Key> KeyFromGlfw(int glfw_key) {
  if (const auto letter = MapRun(glfw_key, GLFW_KEY_A, input::Key::kA, kLetterCount)) {
    return letter;
  }
  if (const auto digit = MapRun(glfw_key, GLFW_KEY_0, input::Key::k0, kDigitCount)) {
    return digit;
  }
  if (const auto function_key = MapRun(glfw_key, GLFW_KEY_F1, input::Key::kF1, kFunctionKeyCount)) {
    return function_key;
  }
  return MapNamedKey(glfw_key);
}

std::optional<input::Key> MouseButtonFromGlfw(int glfw_button) {
  switch (glfw_button) {
    case GLFW_MOUSE_BUTTON_LEFT:
      return input::Key::kMouseLeft;
    case GLFW_MOUSE_BUTTON_RIGHT:
      return input::Key::kMouseRight;
    case GLFW_MOUSE_BUTTON_MIDDLE:
      return input::Key::kMouseMiddle;
    default:
      return std::nullopt;
  }
}

std::optional<input::KeyState> KeyStateFromGlfw(int glfw_action) {
  switch (glfw_action) {
    case GLFW_PRESS:
      return input::KeyState::kPressed;
    case GLFW_RELEASE:
      return input::KeyState::kReleased;
    default:
      return std::nullopt;
  }
}

}  // namespace augusta::renderer
