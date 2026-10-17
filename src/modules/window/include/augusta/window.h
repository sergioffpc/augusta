#ifndef AUGUSTA_WINDOW_H_
#define AUGUSTA_WINDOW_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "augusta/window_events.h"

// GLFW's own window type, which Window::Glfw hands the Renderer without this
// header including GLFW.
struct GLFWwindow;

/// \file
/// augusta::window is the client's single OS window, a GLFW one, and its device
/// events (ADR-0059). It knows no GPU API and no input: the Renderer creates and
/// owns it and makes its Vulkan surface from Glfw(), and the client Runtime
/// drains its events with Poll() on the Main/Render thread and hands them to
/// input. Poll() only drains, apart from presenting a frame, so the Runtime
/// decides when each happens. Every call is from the thread that constructed
/// the Window, GLFW's own rule.
namespace augusta::window {

/// Default initial client-area size, in pixels (see Config::width/height).
constexpr std::uint32_t kDefaultWidth = 1920;
constexpr std::uint32_t kDefaultHeight = 1080;

struct Config {
  /// Shown in the OS title bar and taskbar.
  std::string title;
  /// Initial client-area size, in pixels. The user can resize the window
  /// afterwards through the OS's chrome.
  std::uint32_t width = kDefaultWidth;
  std::uint32_t height = kDefaultHeight;
  /// True to hide the window, for a test that needs no one to see it.
  bool hidden = false;
};

/// A client-area size, in pixels.
struct Size {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

/// The window, with no GPU context of its own: the Renderer's Vulkan surface is
/// made from it. The client makes exactly one.
class Window {
 public:
  /// Opens the window per config. Throws std::runtime_error if GLFW cannot
  /// start or cannot make it.
  explicit Window(const Config& config);
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;

  /// Drains the OS's event queue and returns the keyboard and mouse events
  /// received since the last call, oldest first: the keys and mouse buttons
  /// Key names, as transitions (no key repeat), and cursor moves. Never
  /// waits. Updates what ShouldClose and GetSize return.
  [[nodiscard]] std::vector<Event> Poll();

  /// True once the user has asked the window to close (its close button,
  /// Alt+F4, ...), as of the last Poll. The window stays open until destroyed.
  [[nodiscard]] bool ShouldClose() const;

  /// The client area's size in framebuffer pixels, as of the last Poll (or
  /// since the window opened); a minimized window, at 0 by 0, leaves it as it
  /// was.
  [[nodiscard]] Size GetSize() const;

  /// Hides the OS cursor and captures it for continuous mouselook: cursor moves
  /// keep reporting a position that never stops at the window's edge. GLFW's
  /// disabled cursor, with raw mouse motion where the system has it; GLFW
  /// releases the capture while the window is out of focus and takes it again
  /// when it regains it. Idempotent.
  void SetCursorLocked(bool locked);

  /// The GLFW window, for what only GLFW's own API does with it: the
  /// Renderer's Vulkan surface and its debug HUD's ImGui binding. Its key,
  /// mouse button and cursor callbacks are this Window's; a binding may chain
  /// to them, never replace them.
  [[nodiscard]] GLFWwindow& Glfw() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::window

#endif  // AUGUSTA_WINDOW_H_
