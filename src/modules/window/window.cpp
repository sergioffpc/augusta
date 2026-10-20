#include "augusta/window.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

#include "augusta/window_events.h"
#include "augusta/window_vulkan.h"
#include "glfw_keys.h"
#include "internals.h"

namespace augusta::window {

namespace {

// GLFW's library, initialized for as long as one is alive.
struct GlfwLibrary {
  GlfwLibrary() {
    if (glfwInit() != GLFW_TRUE) {
      throw std::runtime_error("GLFW failed to initialize");
    }
  }
  ~GlfwLibrary() { glfwTerminate(); }
  GlfwLibrary(const GlfwLibrary&) = delete;
  GlfwLibrary& operator=(const GlfwLibrary&) = delete;
  GlfwLibrary(GlfwLibrary&&) = delete;
  GlfwLibrary& operator=(GlfwLibrary&&) = delete;
};

struct GlfwWindowDeleter {
  void operator()(GLFWwindow* window) const { glfwDestroyWindow(window); }
};
using GlfwWindow = std::unique_ptr<GLFWwindow, GlfwWindowDeleter>;

// With no GPU API's context: the Renderer makes a Vulkan surface from it.
GlfwWindow Open(const Config& config) {
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, config.hidden ? GLFW_FALSE : GLFW_TRUE);
  GlfwWindow window(glfwCreateWindow(static_cast<int>(config.width), static_cast<int>(config.height),
                                     config.title.c_str(), nullptr, nullptr));
  if (window == nullptr) {
    throw std::runtime_error("GLFW failed to create the window");
  }
  return window;
}

Size FramebufferSize(GLFWwindow& window) {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(&window, &width, &height);
  return {.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height)};
}

// The window's user pointer is the queue its callbacks append to.
std::vector<Event>& QueueOf(GLFWwindow* window) {
  return *static_cast<std::vector<Event>*>(glfwGetWindowUserPointer(window));
}

// A key's or mouse button's GLFW action, queued if both are ones Key and
// KeyState name.
void QueueKeyEvent(GLFWwindow* window, std::optional<Key> key, int action) {
  const std::optional<KeyState> state = KeyStateFromGlfw(action);
  if (key && state) {
    QueueOf(window).emplace_back(KeyEvent{.key = *key, .state = *state});
  }
}

void OnKey(GLFWwindow* window, int glfw_key, int /*scancode*/, int action, int /*mods*/) {
  QueueKeyEvent(window, KeyFromGlfw(glfw_key), action);
}

void OnMouseButton(GLFWwindow* window, int glfw_button, int action, int /*mods*/) {
  QueueKeyEvent(window, MouseButtonFromGlfw(glfw_button), action);
}

// In client-area pixels; unbounded while the cursor is locked, when GLFW moves
// a virtual cursor instead.
void OnCursorMove(GLFWwindow* window, double x, double y) {
  QueueOf(window).emplace_back(MouseMoveEvent{.x = static_cast<float>(x), .y = static_cast<float>(y)});
}

}  // namespace

struct Window::Impl {
  // Declared in the order they are made, so they are destroyed in reverse.
  GlfwLibrary glfw;
  GlfwWindow window;
  // What the callbacks queued since the last Poll, which glfwPollEvents calls.
  // Not the wheel: Event has none.
  std::vector<Event> events;
  Size size;

  // size starts as the window's own, which Windows may have widened past
  // config's.
  explicit Impl(const Config& config) : window(Open(config)), size(FramebufferSize(*window)) {
    glfwSetWindowUserPointer(window.get(), &events);
    glfwSetKeyCallback(window.get(), OnKey);
    glfwSetMouseButtonCallback(window.get(), OnMouseButton);
    glfwSetCursorPosCallback(window.get(), OnCursorMove);
  }

  std::vector<Event> Poll() {
    glfwPollEvents();
    const Size client_area = FramebufferSize(*window);
    if (client_area.width != 0 && client_area.height != 0) {
      size = client_area;
    }
    return std::exchange(events, {});
  }

  void SetCursorLocked(bool locked) const {
    glfwSetInputMode(window.get(), GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    // Without raw motion, GLFW's disabled cursor still moves as the OS's
    // pointer settings (acceleration) make it.
    if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
      glfwSetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION, locked ? GLFW_TRUE : GLFW_FALSE);
    }
  }
};

Window::Window(const Config& config) : impl_(std::make_unique<Impl>(config)) {}

Window::~Window() = default;

std::vector<Event> Window::Poll() { return impl_->Poll(); }

bool Window::ShouldClose() const { return glfwWindowShouldClose(impl_->window.get()) == GLFW_TRUE; }

Size Window::GetSize() const { return impl_->size; }

void Window::SetCursorLocked(bool locked) { impl_->SetCursorLocked(locked); }

GLFWwindow& Internals::Glfw(const Window& window) { return *window.impl_->window; }

PFN_vkGetInstanceProcAddr VulkanLoader(const Window& /*window*/) {
  if (glfwVulkanSupported() != GLFW_TRUE) {
    return nullptr;
  }
  return reinterpret_cast<PFN_vkGetInstanceProcAddr>(glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr"));
}

std::span<const char* const> VulkanInstanceExtensions(const Window& /*window*/) {
  std::uint32_t count = 0;
  const char** extensions = glfwGetRequiredInstanceExtensions(&count);
  if (extensions == nullptr) {
    return {};
  }
  return {extensions, count};
}

VkResult CreateVulkanSurface(const Window& window, VkInstance instance, VkSurfaceKHR& surface) {
  return glfwCreateWindowSurface(instance, &Internals::Glfw(window), nullptr, &surface);
}

}  // namespace augusta::window
