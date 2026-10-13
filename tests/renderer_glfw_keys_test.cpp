#include <array>
#include <cstddef>
#include <optional>
#include <set>
#include <utility>

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include "augusta/input.h"
#include "glfw_keys.h"

namespace augusta::renderer {
namespace {

using input::Key;
using input::KeyState;

constexpr int kLetterCount = 26;
constexpr int kDigitCount = 10;
constexpr int kFunctionKeyCount = 12;

Key KeyAfter(Key first, int offset) { return static_cast<Key>(static_cast<int>(first) + offset); }

TEST(GlfwKeysTest, MapsEveryLetter) {
  for (int i = 0; i < kLetterCount; ++i) {
    EXPECT_EQ(KeyFromGlfw(GLFW_KEY_A + i), KeyAfter(Key::kA, i)) << "letter " << i;
  }
}

TEST(GlfwKeysTest, MapsEveryDigitAboveTheLetters) {
  for (int i = 0; i < kDigitCount; ++i) {
    EXPECT_EQ(KeyFromGlfw(GLFW_KEY_0 + i), KeyAfter(Key::k0, i)) << "digit " << i;
  }
}

TEST(GlfwKeysTest, MapsF1ToF12) {
  for (int i = 0; i < kFunctionKeyCount; ++i) {
    EXPECT_EQ(KeyFromGlfw(GLFW_KEY_F1 + i), KeyAfter(Key::kF1, i)) << "F" << (i + 1);
  }
}

TEST(GlfwKeysTest, MapsTheNamedKeys) {
  const std::array<std::pair<int, Key>, 15> named = {{
      {GLFW_KEY_SPACE, Key::kSpace},
      {GLFW_KEY_TAB, Key::kTab},
      {GLFW_KEY_ENTER, Key::kEnter},
      {GLFW_KEY_BACKSPACE, Key::kBackspace},
      {GLFW_KEY_ESCAPE, Key::kEscape},
      {GLFW_KEY_LEFT_SHIFT, Key::kLeftShift},
      {GLFW_KEY_RIGHT_SHIFT, Key::kRightShift},
      {GLFW_KEY_LEFT_CONTROL, Key::kLeftControl},
      {GLFW_KEY_RIGHT_CONTROL, Key::kRightControl},
      {GLFW_KEY_LEFT_ALT, Key::kLeftAlt},
      {GLFW_KEY_RIGHT_ALT, Key::kRightAlt},
      {GLFW_KEY_UP, Key::kUp},
      {GLFW_KEY_DOWN, Key::kDown},
      {GLFW_KEY_LEFT, Key::kLeft},
      {GLFW_KEY_RIGHT, Key::kRight},
  }};
  for (const auto& [glfw_key, key] : named) {
    EXPECT_EQ(KeyFromGlfw(glfw_key), key) << input::NameOf(key);
  }
}

TEST(GlfwKeysTest, MapsNothingElse) {
  EXPECT_EQ(KeyFromGlfw(GLFW_KEY_UNKNOWN), std::nullopt);
  EXPECT_EQ(KeyFromGlfw(GLFW_KEY_KP_0), std::nullopt);
  EXPECT_EQ(KeyFromGlfw(GLFW_KEY_KP_ENTER), std::nullopt);
  EXPECT_EQ(KeyFromGlfw(GLFW_KEY_F13), std::nullopt);
  EXPECT_EQ(KeyFromGlfw(GLFW_KEY_LEFT_SUPER), std::nullopt);
  EXPECT_EQ(KeyFromGlfw(GLFW_KEY_GRAVE_ACCENT), std::nullopt);
  EXPECT_EQ(KeyFromGlfw(GLFW_KEY_LAST + 1), std::nullopt);
}

std::size_t KeyboardKeyCount() {
  std::size_t count = 0;
  for (std::size_t i = 0; i < input::kKeyCount; ++i) {
    count += input::IsMouseButton(static_cast<Key>(i)) ? 0 : 1;
  }
  return count;
}

TEST(GlfwKeysTest, ReachesEveryKeyButTheMouseButtons) {
  std::set<Key> reached;
  for (int glfw_key = GLFW_KEY_UNKNOWN; glfw_key <= GLFW_KEY_LAST; ++glfw_key) {
    if (const std::optional<Key> key = KeyFromGlfw(glfw_key)) {
      EXPECT_FALSE(input::IsMouseButton(*key)) << input::NameOf(*key);
      EXPECT_TRUE(reached.insert(*key).second) << input::NameOf(*key) << " from two GLFW keys";
    }
  }

  EXPECT_EQ(reached.size(), KeyboardKeyCount());
}

TEST(GlfwKeysTest, MapsTheLeftRightAndMiddleMouseButtons) {
  EXPECT_EQ(MouseButtonFromGlfw(GLFW_MOUSE_BUTTON_LEFT), Key::kMouseLeft);
  EXPECT_EQ(MouseButtonFromGlfw(GLFW_MOUSE_BUTTON_RIGHT), Key::kMouseRight);
  EXPECT_EQ(MouseButtonFromGlfw(GLFW_MOUSE_BUTTON_MIDDLE), Key::kMouseMiddle);
  EXPECT_EQ(MouseButtonFromGlfw(GLFW_MOUSE_BUTTON_4), std::nullopt);
}

TEST(GlfwKeysTest, TakesPressAndReleaseButNotRepeat) {
  EXPECT_EQ(KeyStateFromGlfw(GLFW_PRESS), KeyState::kPressed);
  EXPECT_EQ(KeyStateFromGlfw(GLFW_RELEASE), KeyState::kReleased);
  EXPECT_EQ(KeyStateFromGlfw(GLFW_REPEAT), std::nullopt);
}

}  // namespace
}  // namespace augusta::renderer
