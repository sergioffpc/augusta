#include <cstddef>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

#include "augusta/debug_shapes.h"
#include "augusta/math.h"
#include "augusta/renderer.h"
#include "renderer_smoke_test.h"

// The Debug overlay shapes the Renderer draws, in a Development build only
// (ADR-0074, ADR-0075).
namespace augusta::renderer::smoke_test {
namespace {

constexpr std::size_t kRed = 0;
constexpr std::size_t kGreen = 1;
constexpr std::size_t kBlue = 2;

constexpr std::uint8_t kFull = 255;

constexpr math::Vec3 kPureRed{1.0F, 0.0F, 0.0F};
constexpr math::Vec3 kPureGreen{0.0F, 1.0F, 0.0F};

std::span<const std::uint8_t> Pixel(const CapturedFrame& frame, std::uint32_t x, std::uint32_t y) {
  return std::span(frame.pixels).subspan(((std::size_t{y} * frame.width) + x) * kBytesPerPixel, kBytesPerPixel);
}

// Exactly color's sRGB bytes, which are 0 or 255 for a pure colour drawn
// opaque.
bool IsExactly(std::span<const std::uint8_t> rgba, const math::Vec3& color) {
  for (std::size_t i = 0; i < 3; ++i) {
    if (rgba[i] != (color[static_cast<int>(i)] > 0.0F ? kFull : 0)) {
      return false;
    }
  }
  return rgba[3] == kOpaque;
}

// Looking down -Z at the plane z = 0 from 5 m away, which then spans about
// +-3.2 m across the frame and +-2.4 m up it, centred on the origin: a pixel
// there is about 2 cm on a side.
const Camera kCamera{.position = {0.0F, 0.0F, 5.0F}, .rotation = {1.0F, 0.0F, 0.0F, 0.0F}, .vertical_fov = 0.9F};

// Where the frame shows the origin and the scene's blue square 1.7 m to its
// right.
constexpr std::uint32_t kCentreX = kWidth / 2;
constexpr std::uint32_t kCentreY = kHeight / 2;
constexpr std::uint32_t kRightX = 244;

// The height of the middle of the pixel row just below the frame's centre, in
// the plane z = 0, where a horizontal segment lights that row alone.
constexpr float kCentreRowY = -0.01F;

// The debug HUD's first line sits in the frame's top rows: its window 10 pixels
// in, its padding, and one line of text.
constexpr std::uint32_t kHudBottom = 48;

// A blue 1 m square in the plane z = 0, 1.7 m right of the origin.
void SetWorld(Renderer& renderer) {
  const SceneMesh square{
      .positions = {{1.2F, -0.5F, 0.0F}, {2.2F, -0.5F, 0.0F}, {2.2F, 0.5F, 0.0F}, {1.2F, 0.5F, 0.0F}},
      .indices = {0, 1, 2, 0, 2, 3},
      .color = {0.0F, 0.0F, 1.0F}};
  renderer.SetScene({.meshes = {square}, .camera = {}});
  renderer.SetCamera(kCamera);
}

// A triangle in the plane z = depth, covering the whole frame there and
// beyond.
debug::Triangle FrameCoveringTriangle(float depth, const math::Vec3& color, float alpha, bool through_walls) {
  return {.corners = {{{-20.0F, -10.0F, depth}, {20.0F, -10.0F, depth}, {0.0F, 20.0F, depth}}},
          .color = color,
          .alpha = alpha,
          .through_walls = through_walls};
}

class RendererDebugShapesSmokeTest : public RendererSmokeTest {
 protected:
  // Draws more frames than are in flight, each handed shapes, as the client
  // Runtime hands them every frame, and reads the last one back.
  CapturedFrame RenderShapes(const debug::Shapes& shapes) {
    for (int i = 0; i < 4; ++i) {
      static_cast<void>(window_.Poll());
      renderer_->SetDebugShapes(shapes);
      renderer_->RenderFrame();
    }
    return renderer_->CaptureFrame();
  }
};

TEST_F(RendererDebugShapesSmokeTest, DrawsASegmentInItsColourWhereTheCameraSeesIt) {
  SetWorld(*renderer_);
  debug::Shapes shapes;
  shapes.segments.push_back({.from = {-1.0F, kCentreRowY, 0.0F},
                             .to = {1.0F, kCentreRowY, 0.0F},
                             .color = kPureGreen,
                             .alpha = 1.0F,
                             .through_walls = false});

  const CapturedFrame frame = RenderShapes(shapes);

  ASSERT_EQ(frame.width, kWidth);
  EXPECT_TRUE(IsExactly(Pixel(frame, kCentreX, kCentreY), kPureGreen));
  // One pixel wide: the rows on either side show the clear frame.
  EXPECT_TRUE(IsClearColor(Pixel(frame, kCentreX, kCentreY - 2)));
  EXPECT_TRUE(IsClearColor(Pixel(frame, kCentreX, kCentreY + 2)));
}

TEST_F(RendererDebugShapesSmokeTest, HidesAShapeBehindTheScene) {
  SetWorld(*renderer_);
  debug::Shapes shapes;
  shapes.triangles.push_back(FrameCoveringTriangle(-1.0F, kPureRed, 1.0F, false));

  const CapturedFrame frame = RenderShapes(shapes);

  ASSERT_EQ(frame.width, kWidth);
  // Drawn where nothing is in front of it, and hidden behind the square.
  EXPECT_TRUE(IsExactly(Pixel(frame, kCentreX, kCentreY), kPureRed));
  EXPECT_FALSE(IsExactly(Pixel(frame, kRightX, kCentreY), kPureRed));
  EXPECT_GT(Pixel(frame, kRightX, kCentreY)[kBlue], kClearChannel + kTolerance);
}

TEST_F(RendererDebugShapesSmokeTest, DrawsAShapeMarkedThroughWallsOverTheScene) {
  SetWorld(*renderer_);
  debug::Shapes shapes;
  shapes.triangles.push_back(FrameCoveringTriangle(-1.0F, kPureRed, 1.0F, true));

  const CapturedFrame frame = RenderShapes(shapes);

  ASSERT_EQ(frame.width, kWidth);
  EXPECT_TRUE(IsExactly(Pixel(frame, kRightX, kCentreY), kPureRed));
}

TEST_F(RendererDebugShapesSmokeTest, BlendsATranslucentShapeWithWhatIsBehindIt) {
  SetWorld(*renderer_);
  const CapturedFrame without = RenderShapes({});
  const std::span<const std::uint8_t> scene = Pixel(without, kRightX, kCentreY);
  debug::Shapes shapes;
  shapes.triangles.push_back(FrameCoveringTriangle(0.1F, kPureRed, 0.5F, false));

  const CapturedFrame frame = RenderShapes(shapes);

  ASSERT_EQ(frame.width, kWidth);
  const std::span<const std::uint8_t> blended = Pixel(frame, kRightX, kCentreY);
  // Redder than the square, but still showing its blue, dimmed.
  EXPECT_GT(blended[kRed], scene[kRed]);
  EXPECT_LT(blended[kRed], kFull);
  EXPECT_LT(blended[kBlue], scene[kBlue]);
  EXPECT_GT(blended[kBlue], kClearChannel + kTolerance);
  EXPECT_EQ(blended[kGreen], 0);
  EXPECT_EQ(blended[3], kOpaque);
}

TEST_F(RendererDebugShapesSmokeTest, DrawsShapesUnderTheDebugHud) {
  renderer_->SetCamera(kCamera);
  debug::Shapes shapes;
  shapes.triangles.push_back(FrameCoveringTriangle(0.0F, kPureRed, 1.0F, true));
  const auto other_than_red = [](const CapturedFrame& frame) {
    std::size_t count = 0;
    for (std::uint32_t y = 0; y < kHudBottom; ++y) {
      for (std::uint32_t x = 0; x < frame.width; ++x) {
        count += IsExactly(Pixel(frame, x, y), kPureRed) ? 0 : 1;
      }
    }
    return count;
  };
  ASSERT_EQ(other_than_red(RenderShapes(shapes)), 0U);

  renderer_->SetTextLines({"FPS: 144 (6.9ms) | RTT: --"});
  const CapturedFrame frame = RenderShapes(shapes);

  ASSERT_EQ(frame.width, kWidth);
  EXPECT_GT(other_than_red(frame), 0U);
}

TEST_F(RendererDebugShapesSmokeTest, DrawsTheFrameAsBeforeOnceHandedNoShapes) {
  SetWorld(*renderer_);
  const CapturedFrame before = RenderFrames();
  debug::Shapes shapes;
  shapes.triangles.push_back(FrameCoveringTriangle(0.1F, kPureGreen, 1.0F, true));
  ASSERT_NE(RenderShapes(shapes).pixels, before.pixels);

  const CapturedFrame frame = RenderShapes({});

  EXPECT_EQ(frame.pixels, before.pixels);
}

}  // namespace
}  // namespace augusta::renderer::smoke_test
