#ifndef AUGUSTA_RENDERER_GLFW_KEYS_H_
#define AUGUSTA_RENDERER_GLFW_KEYS_H_

#include <optional>

#include "augusta/input.h"

/// \file
/// Which input::Key a GLFW key or mouse button is, and which KeyState a GLFW
/// action is: what nvrhi_renderer.cpp's window callbacks push to
/// input::EventSink (ADR-0059), the same keys Falcor's Renderer maps. Kept apart
/// from the window, which needs a display, so the mapping is tested on its own.
/// Private to the module, like vertices.h.
namespace augusta::renderer {

/// The key a GLFW key code (GLFW_KEY_*) is; nullopt for a key input::Key does
/// not name, such as the keypad's.
[[nodiscard]] std::optional<input::Key> KeyFromGlfw(int glfw_key);

/// The key a GLFW mouse button (GLFW_MOUSE_BUTTON_*) is; nullopt past the
/// middle button.
[[nodiscard]] std::optional<input::Key> MouseButtonFromGlfw(int glfw_button);

/// The transition a GLFW action is; nullopt for GLFW_REPEAT, which is no
/// transition: Input tracks a held key itself.
[[nodiscard]] std::optional<input::KeyState> KeyStateFromGlfw(int glfw_action);

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_GLFW_KEYS_H_
