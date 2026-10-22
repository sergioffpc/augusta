#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/debug_panels.h"
#include "augusta/renderer.h"
#include "augusta/window_events.h"
#include "renderer_smoke_test.h"

// The Debug panels the Renderer draws and operates through Dear ImGui, in a
// Development build only (ADR-0074, ADR-0075).
namespace augusta::renderer::smoke_test {
namespace {

// The first panel's window opens with its top left here, under the debug HUD.
constexpr std::uint32_t kPanelLeft = 10;
constexpr std::uint32_t kPanelTop = 60;
// How far down and right each next panel opens from the one before it.
constexpr float kPanelCascade = 30.0F;

// A point inside a panel's first element, when it is a button: under the
// panel's title bar and its padding, a few pixels into the button.
constexpr float kFirstButtonX = kPanelLeft + 14.0F;
constexpr float kFirstButtonY = kPanelTop + 36.0F;

// Columns [left, right) of rows [top, bottom) of a frame.
struct Region {
  std::uint32_t left = 0;
  std::uint32_t top = 0;
  std::uint32_t right = kWidth;
  std::uint32_t bottom = kHeight;
};

// How many of frame's pixels in region are not the clear color.
std::size_t CountOtherPixelsIn(const CapturedFrame& frame, const Region& region) {
  std::size_t other_pixels = 0;
  for (std::uint32_t row = region.top; row < region.bottom; ++row) {
    for (std::uint32_t column = region.left; column < region.right; ++column) {
      const std::size_t first = ((std::size_t{row} * frame.width) + column) * kBytesPerPixel;
      if (!IsClearColor(std::span(frame.pixels).subspan(first, kBytesPerPixel))) {
        ++other_pixels;
      }
    }
  }
  return other_pixels;
}

debug::Panel PanelWithButton(bool disabled) {
  return {.id = "profiling",
          .title = "Profiling",
          .elements = {debug::Button{.id = "capture", .label = "Capture 300 frames", .disabled = disabled}}};
}

class DebugPanelsSmokeTest : public RendererSmokeTest {
 protected:
  // Clicks at (x, y) as a developer would: the cursor moves there and the left
  // button goes down, then, a frame later, comes up.
  void ClickAt(float x, float y) {
    const std::vector<window::Event> press{
        window::MouseMoveEvent{.x = x, .y = y},
        window::KeyEvent{.key = window::Key::kMouseLeft, .state = window::KeyState::kPressed}};
    const std::vector<window::Event> release{
        window::KeyEvent{.key = window::Key::kMouseLeft, .state = window::KeyState::kReleased}};
    renderer_->HandleDebugPanelInput(press);
    RenderFrames();
    renderer_->HandleDebugPanelInput(release);
    RenderFrames();
  }
};

TEST_F(DebugPanelsSmokeTest, DrawsAPanelInItsDefaultRegion) {
  renderer_->SetDebugPanels({PanelWithButton(false)});

  const CapturedFrame frame = RenderFrames();

  ASSERT_EQ(frame.height, kHeight);
  EXPECT_GT(CountOtherPixelsIn(frame, {.left = kPanelLeft, .top = kPanelTop}), 0U);
  EXPECT_EQ(CountOtherPixels(frame, 0, kPanelTop), 0U);
  EXPECT_EQ(CountOtherPixelsIn(frame, {.right = kPanelLeft}), 0U);
}

TEST_F(DebugPanelsSmokeTest, DrawsNothingForNoPanels) {
  renderer_->SetDebugPanels({PanelWithButton(false)});
  RenderFrames();
  renderer_->SetDebugPanels({});

  const CapturedFrame frame = RenderFrames();

  ASSERT_EQ(frame.height, kHeight);
  EXPECT_EQ(CountOtherPixels(frame, 0, kHeight), 0U);
  EXPECT_TRUE(renderer_->TakeDebugControlEvents().empty());
}

TEST_F(DebugPanelsSmokeTest, HandsBackAClickedButtonsPress) {
  renderer_->SetDebugPanels({PanelWithButton(false)});
  RenderFrames();

  ClickAt(kFirstButtonX, kFirstButtonY);

  EXPECT_EQ(
      renderer_->TakeDebugControlEvents(),
      (std::vector<debug::ControlEvent>{{.panel = "profiling", .control = "capture", .value = debug::ButtonPress{}}}));
  EXPECT_TRUE(renderer_->TakeDebugControlEvents().empty());
}

TEST_F(DebugPanelsSmokeTest, HandsBackNothingForAGreyedOutButton) {
  renderer_->SetDebugPanels({PanelWithButton(false)});
  RenderFrames();
  ClickAt(kFirstButtonX, kFirstButtonY);  // Control: not greyed out, the click presses it.
  ASSERT_EQ(renderer_->TakeDebugControlEvents().size(), 1U);
  renderer_->SetDebugPanels({PanelWithButton(true)});
  RenderFrames();

  ClickAt(kFirstButtonX, kFirstButtonY);

  EXPECT_TRUE(renderer_->TakeDebugControlEvents().empty());
}

TEST_F(DebugPanelsSmokeTest, HandsBackAClickedTogglesNewState) {
  renderer_->SetDebugPanels(
      {{.id = "index", .title = "Debug", .elements = {debug::Toggle{.id = "hud", .label = "HUD", .on = true}}}});
  RenderFrames();

  ClickAt(kFirstButtonX, kFirstButtonY);

  EXPECT_EQ(renderer_->TakeDebugControlEvents(),
            (std::vector<debug::ControlEvent>{{.panel = "index", .control = "hud", .value = false}}));
}

// A panel opens at a default place of its own, the next one free when ImGui
// first sees it, not at its place in the frame's list: one shown alone first
// keeps the first place when another comes before it in the list.
TEST_F(DebugPanelsSmokeTest, KeepsEachPanelsDefaultPlaceWhenTheListsOrderChanges) {
  const debug::Panel first{
      .id = "first", .title = "First", .elements = {debug::Button{.id = "press", .label = "First's button"}}};
  const debug::Panel second{
      .id = "second", .title = "Second", .elements = {debug::Button{.id = "press", .label = "Second's button"}}};
  renderer_->SetDebugPanels({first});
  RenderFrames();
  renderer_->SetDebugPanels({second, first});
  RenderFrames();

  ClickAt(kFirstButtonX, kFirstButtonY);
  ClickAt(kFirstButtonX + kPanelCascade, kFirstButtonY + kPanelCascade);

  EXPECT_EQ(renderer_->TakeDebugControlEvents(),
            (std::vector<debug::ControlEvent>{{.panel = "first", .control = "press", .value = debug::ButtonPress{}},
                                              {.panel = "second", .control = "press", .value = debug::ButtonPress{}}}));
}

// lavapipe is no NVIDIA GPU, so Aftermath's diagnostics are never on its
// device, whether Aftermath started or not; a dump would still land somewhere.
TEST_F(DebugPanelsSmokeTest, ReportsAftermathUnattachedOnLavapipe) {
  const GpuCrashDumpStatus status = renderer_->GpuCrashDumps();

  EXPECT_FALSE(status.attached);
  EXPECT_TRUE(status.directory.is_absolute());
}

}  // namespace
}  // namespace augusta::renderer::smoke_test
