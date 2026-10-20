#ifndef AUGUSTA_WINDOW_H_
#define AUGUSTA_WINDOW_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "augusta/window_events.h"

/// \file
/// augusta::window is the client's single OS window and its device events
/// (ADR-0059); the windowing library it opens them with stays inside the
/// module. Of GPU APIs it knows only Vulkan's handles, to make the surface the
/// Renderer presents to (window_vulkan.h), and it knows no input: the client
/// Runtime owns it and lends it to the Renderer, and drains its events with
/// Poll() on the Main/Render thread and hands them to input. Poll() only drains, apart from presenting a
/// frame, so the Runtime decides when each happens. Every call is from the
/// thread that constructed the Window, the windowing library's own rule.
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
  /// Opens the window per config. Throws std::runtime_error if the windowing
  /// library cannot start or cannot make it.
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
  /// keep reporting a position that never stops at the window's edge, with raw
  /// mouse motion where the system has it. The capture is released while the
  /// window is out of focus and taken again when it regains it. Idempotent.
  void SetCursorLocked(bool locked);

 private:
  // The module's private access to what the window is made with
  // (internals.h), for its tests.
  friend struct Internals;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::window

#endif  // AUGUSTA_WINDOW_H_
