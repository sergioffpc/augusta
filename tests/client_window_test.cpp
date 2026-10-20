#include <cstdint>
#include <variant>
#include <vector>

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include "augusta/window.h"
#include "augusta/window_events.h"
#include "internals.h"

namespace augusta::window {
namespace {

// What the OS delivers to the window, as GLFW hands it on: through the
// callbacks the window installed on its GLFW window, which glfwPollEvents
// calls. A test has no OS events to make, so it calls them as GLFW would.
class OsEvents {
 public:
  explicit OsEvents(GLFWwindow& window)
      : key_(glfwSetKeyCallback(&window, nullptr)),
        mouse_button_(glfwSetMouseButtonCallback(&window, nullptr)),
        cursor_(glfwSetCursorPosCallback(&window, nullptr)),
        window_(&window) {
    glfwSetKeyCallback(window_, key_);
    glfwSetMouseButtonCallback(window_, mouse_button_);
    glfwSetCursorPosCallback(window_, cursor_);
  }

  void Key(int glfw_key, int action) const { key_(window_, glfw_key, 0, action, 0); }
  void MouseButton(int glfw_button, int action) const { mouse_button_(window_, glfw_button, action, 0); }
  void CursorAt(double x, double y) const { cursor_(window_, x, y); }

 private:
  GLFWkeyfun key_;
  GLFWmousebuttonfun mouse_button_;
  GLFWcursorposfun cursor_;
  GLFWwindow* window_;
};

constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 240;

class WindowTest : public ::testing::Test {
 protected:
  Window window_{{.title = "augusta window test", .width = kWidth, .height = kHeight, .hidden = true}};
  OsEvents os_{Internals::Glfw(window_)};
};

TEST_F(WindowTest, PollsNoEventsBeforeTheOsDeliversAny) { EXPECT_TRUE(window_.Poll().empty()); }

TEST_F(WindowTest, PollsTheKeyAndMouseEventsSinceTheLastPollInOrder) {
  os_.Key(GLFW_KEY_W, GLFW_PRESS);
  os_.CursorAt(10.0, 20.0);
  os_.MouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
  os_.Key(GLFW_KEY_W, GLFW_RELEASE);

  const std::vector<Event> events = window_.Poll();

  ASSERT_EQ(events.size(), 4U);
  ASSERT_TRUE(std::holds_alternative<KeyEvent>(events[0]));
  EXPECT_EQ(std::get<KeyEvent>(events[0]).key, Key::kW);
  EXPECT_EQ(std::get<KeyEvent>(events[0]).state, KeyState::kPressed);
  ASSERT_TRUE(std::holds_alternative<MouseMoveEvent>(events[1]));
  EXPECT_FLOAT_EQ(std::get<MouseMoveEvent>(events[1]).x, 10.0F);
  EXPECT_FLOAT_EQ(std::get<MouseMoveEvent>(events[1]).y, 20.0F);
  ASSERT_TRUE(std::holds_alternative<KeyEvent>(events[2]));
  EXPECT_EQ(std::get<KeyEvent>(events[2]).key, Key::kMouseLeft);
  EXPECT_EQ(std::get<KeyEvent>(events[2]).state, KeyState::kPressed);
  ASSERT_TRUE(std::holds_alternative<KeyEvent>(events[3]));
  EXPECT_EQ(std::get<KeyEvent>(events[3]).key, Key::kW);
  EXPECT_EQ(std::get<KeyEvent>(events[3]).state, KeyState::kReleased);
}

TEST_F(WindowTest, PollsEachEventOnce) {
  os_.Key(GLFW_KEY_SPACE, GLFW_PRESS);
  ASSERT_EQ(window_.Poll().size(), 1U);

  EXPECT_TRUE(window_.Poll().empty());
}

TEST_F(WindowTest, PollsNoKeyRepeatsAndNoKeysItDoesNotName) {
  os_.Key(GLFW_KEY_A, GLFW_REPEAT);
  os_.Key(GLFW_KEY_KP_5, GLFW_PRESS);
  os_.MouseButton(GLFW_MOUSE_BUTTON_4, GLFW_PRESS);

  EXPECT_TRUE(window_.Poll().empty());
}

TEST_F(WindowTest, ReportsTheConfiguredSizeUntilClosed) {
  static_cast<void>(window_.Poll());

  EXPECT_EQ(window_.GetSize().width, kWidth);
  EXPECT_EQ(window_.GetSize().height, kHeight);
  EXPECT_FALSE(window_.ShouldClose());
}

}  // namespace
}  // namespace augusta::window
