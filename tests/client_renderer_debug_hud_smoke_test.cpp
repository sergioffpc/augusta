#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "augusta/renderer.h"
#include "renderer_smoke_test.h"

// The debug HUD the Renderer draws, in a Development build only (ADR-0074).
namespace augusta::renderer::smoke_test {
namespace {

// The debug HUD's first line sits in the frame's top rows: its window 10 pixels
// in, its padding, and one line of text.
constexpr std::uint32_t kHudBottom = 48;

TEST_F(RendererSmokeTest, ClearsEveryPixelBelowTheDebugHudToTheClearColor) {
  renderer_->SetTextLines({"FPS: 144 (6.9ms) | RTT: --"});
  RenderFrames();

  const CapturedFrame frame = renderer_->CaptureFrame();

  ASSERT_EQ(frame.width, kWidth);
  ASSERT_EQ(frame.height, kHeight);
  ASSERT_EQ(frame.pixels.size(), std::size_t{kWidth} * kHeight * kBytesPerPixel);
  EXPECT_EQ(CountOtherPixels(frame, kHudBottom, kHeight), 0U);
}

TEST_F(RendererSmokeTest, DrawsNoDebugHudBeforeItHasLines) {
  const CapturedFrame frame = RenderFrames();

  ASSERT_EQ(frame.height, kHeight);
  EXPECT_EQ(CountOtherPixels(frame, 0, kHeight), 0U);
}

TEST_F(RendererSmokeTest, DrawsTheDebugHudsLinesInItsRegion) {
  renderer_->SetTextLines({"FPS: --"});
  const std::size_t short_line = CountOtherPixels(RenderFrames(), 0, kHudBottom);

  renderer_->SetTextLines({"FPS: -- | RTT: 12ms | Jitter: 0.4ms | Loss: 0.0% | In: 1.0KB/s | Out: 512B/s"});
  const CapturedFrame frame = RenderFrames();

  ASSERT_EQ(frame.height, kHeight);
  EXPECT_GT(short_line, 0U);
  EXPECT_GT(CountOtherPixels(frame, 0, kHudBottom), short_line);
}

}  // namespace
}  // namespace augusta::renderer::smoke_test
