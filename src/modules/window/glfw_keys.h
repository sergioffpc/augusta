#ifndef AUGUSTA_WINDOW_GLFW_KEYS_H_
#define AUGUSTA_WINDOW_GLFW_KEYS_H_

#include <optional>

#include "augusta/window_events.h"

/// \file
/// Which Key a GLFW key or mouse button is, and which KeyState a GLFW
/// action is: what window.cpp's callbacks turn GLFW's events into (ADR-0059).
/// Kept apart from the window, which needs a display, so the mapping is tested
/// on its own. Private to the module.
namespace augusta::window {

/// The key a GLFW key code (GLFW_KEY_*) is; nullopt for a key Key does not
/// name, such as the keypad's.
[[nodiscard]] std::optional<Key> KeyFromGlfw(int glfw_key);

/// The key a GLFW mouse button (GLFW_MOUSE_BUTTON_*) is; nullopt past the
/// middle button.
[[nodiscard]] std::optional<Key> MouseButtonFromGlfw(int glfw_button);

/// The transition a GLFW action is; nullopt for GLFW_REPEAT, which is no
/// transition: a reader tracks a held key itself.
[[nodiscard]] std::optional<KeyState> KeyStateFromGlfw(int glfw_action);

}  // namespace augusta::window

#endif  // AUGUSTA_WINDOW_GLFW_KEYS_H_
