#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>

#include <gtest/gtest.h>

#include "augusta/input.h"
#include "augusta/renderer.h"

namespace augusta::renderer {
namespace {

// Small, so WARP draws it fast, but wider than Windows' narrowest window with a
// title bar.
constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 240;

// The Renderer's near-black clear color, 0.016 in linear light, as an sRGB
// byte, give or take the GPU's rounding.
constexpr int kClearChannel = 34;
constexpr int kTolerance = 1;
constexpr std::uint8_t kOpaque = 255;

// The debug HUD's line sits in the frame's top rows: its window 10 pixels in,
// its padding, and one line of text.
constexpr std::uint32_t kHudBottom = 48;

constexpr std::size_t kBytesPerPixel = 4;

bool IsClearColor(std::span<const std::uint8_t> rgba) {
  const auto near_clear = [](std::uint8_t channel) { return std::abs(channel - kClearChannel) <= kTolerance; };
  return near_clear(rgba[0]) && near_clear(rgba[1]) && near_clear(rgba[2]) && rgba[3] == kOpaque;
}

// How many of frame's pixels in rows [first_row, last_row) are not the clear
// color.
std::size_t CountOtherPixels(const CapturedFrame& frame, std::uint32_t first_row, std::uint32_t last_row) {
  const std::size_t row_size = std::size_t{frame.width} * kBytesPerPixel;
  const auto rows = std::span(frame.pixels).subspan(first_row * row_size, (last_row - first_row) * row_size);
  std::size_t other_pixels = 0;
  for (std::size_t i = 0; i < rows.size(); i += kBytesPerPixel) {
    if (!IsClearColor(rows.subspan(i, kBytesPerPixel))) {
      ++other_pixels;
    }
  }
  return other_pixels;
}

class IgnoredInput final : public input::EventSink {
 public:
  void OnKeyEvent(const input::KeyEvent& /*event*/) override {}
  void OnMouseMoveEvent(const input::MouseMoveEvent& /*event*/) override {}
};

// What CI's runner, which has no GPU, can run.
Config SoftwareHiddenConfig() {
  return {.title = "augusta renderer smoke test",
          .width = kWidth,
          .height = kHeight,
          .adapter = Adapter::kSoftware,
          .hidden = true};
}

class RendererSmokeTest : public ::testing::Test {
 protected:
  // More frames than are in flight, so the swapchain's images all come round.
  void RenderFrames() {
    for (int i = 0; i < 4; ++i) {
      renderer_->PumpEvents();
      renderer_->RenderFrame();
    }
  }

  IgnoredInput input_;
  std::unique_ptr<Renderer> renderer_ = std::make_unique<Renderer>(SoftwareHiddenConfig(), input_);
};

TEST_F(RendererSmokeTest, ReportsTheConfiguredSize) {
  const Size size = renderer_->GetSize();

  EXPECT_EQ(size.width, kWidth);
  EXPECT_EQ(size.height, kHeight);
}

TEST_F(RendererSmokeTest, CapturesNothingBeforeTheFirstFrame) {
  const CapturedFrame frame = renderer_->CaptureFrame();

  EXPECT_EQ(frame.width, 0U);
  EXPECT_EQ(frame.height, 0U);
  EXPECT_TRUE(frame.pixels.empty());
}

TEST_F(RendererSmokeTest, ClearsEveryPixelBelowTheDebugHudToTheClearColor) {
  RenderFrames();

  const CapturedFrame frame = renderer_->CaptureFrame();

  ASSERT_EQ(frame.width, kWidth);
  ASSERT_EQ(frame.height, kHeight);
  ASSERT_EQ(frame.pixels.size(), std::size_t{kWidth} * kHeight * kBytesPerPixel);
  EXPECT_EQ(CountOtherPixels(frame, kHudBottom, kHeight), 0U);
}

TEST_F(RendererSmokeTest, DrawsTheDebugHudsStatsInItsRegion) {
  RenderFrames();
  const std::size_t without_stats = CountOtherPixels(renderer_->CaptureFrame(), 0, kHudBottom);

  renderer_->SetDebugHudStats({.net = DebugHudNetStats{.rtt_ms = 12,
                                                       .jitter_ms = 0.4F,
                                                       .loss_percent = 0.0F,
                                                       .in_bytes_per_sec = 1024.0F,
                                                       .out_bytes_per_sec = 512.0F}});
  RenderFrames();

  const CapturedFrame frame = renderer_->CaptureFrame();

  ASSERT_EQ(frame.height, kHeight);
  // The connection's numbers make the line longer than "RTT: --".
  EXPECT_GT(without_stats, 0U);
  EXPECT_GT(CountOtherPixels(frame, 0, kHudBottom), without_stats);
}

TEST_F(RendererSmokeTest, TearsDownSoThatAnotherRendererCanStart) {
  renderer_->RenderFrame();
  renderer_.reset();

  renderer_ = std::make_unique<Renderer>(SoftwareHiddenConfig(), input_);
  renderer_->RenderFrame();

  EXPECT_EQ(renderer_->CaptureFrame().width, kWidth);
}

}  // namespace
}  // namespace augusta::renderer
