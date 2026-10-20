#ifndef AUGUSTA_WINDOW_INTERNALS_H_
#define AUGUSTA_WINDOW_INTERNALS_H_

#include <GLFW/glfw3.h>

#include "augusta/window.h"

/// \file
/// The window module's access to the GLFW window a Window is made with,
/// private to the module: its tests reach it through the module's directory,
/// as they do glfw_keys.h, to call the window's GLFW callbacks as the OS's
/// events would. Nothing outside the module sees GLFW.
namespace augusta::window {

struct Internals {
  /// window's GLFW window. Its key, mouse button and cursor callbacks are the
  /// Window's.
  static GLFWwindow& Glfw(const Window& window);
};

}  // namespace augusta::window

#endif  // AUGUSTA_WINDOW_INTERNALS_H_
