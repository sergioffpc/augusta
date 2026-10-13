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

// The Renderer's near-black clear colour, 0.016 in linear light, as an sRGB
// byte, give or take the GPU's rounding.
constexpr int kClearChannel = 34;
constexpr int kTolerance = 1;
constexpr std::uint8_t kOpaque = 255;

bool IsClearColour(std::span<const std::uint8_t> rgba) {
  const auto near_clear = [](std::uint8_t channel) { return std::abs(channel - kClearChannel) <= kTolerance; };
  return near_clear(rgba[0]) && near_clear(rgba[1]) && near_clear(rgba[2]) && rgba[3] == kOpaque;
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

TEST_F(RendererSmokeTest, ClearsEveryPixelToTheClearColour) {
  // More frames than are in flight, so the swapchain's images all come round.
  for (int i = 0; i < 4; ++i) {
    renderer_->PumpEvents();
    renderer_->RenderFrame();
  }

  const CapturedFrame frame = renderer_->CaptureFrame();

  ASSERT_EQ(frame.width, kWidth);
  ASSERT_EQ(frame.height, kHeight);
  ASSERT_EQ(frame.pixels.size(), std::size_t{kWidth} * kHeight * 4);
  std::size_t other_pixels = 0;
  for (std::size_t i = 0; i < frame.pixels.size(); i += 4) {
    if (!IsClearColour(std::span(frame.pixels).subspan(i, 4))) {
      ++other_pixels;
    }
  }
  EXPECT_EQ(other_pixels, 0U);
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
