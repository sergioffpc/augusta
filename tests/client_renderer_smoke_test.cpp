#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/renderer.h"
#include "augusta/window.h"

namespace augusta::renderer {
namespace {

// Small, so lavapipe draws it fast, but wider than Windows' narrowest window with a
// title bar.
constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 240;

// The Renderer's near-black clear color, 0.016 in linear light, as an sRGB
// byte, give or take the GPU's rounding.
constexpr int kClearChannel = 34;
constexpr int kTolerance = 1;
constexpr std::uint8_t kOpaque = 255;

#if AUGUSTA_DEBUG_TOOLS
// The debug HUD's first line sits in the frame's top rows: its window 10 pixels
// in, its padding, and one line of text.
constexpr std::uint32_t kHudBottom = 48;
#endif

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

// A surface of one pure colour, shaded: lit in that channel alone, whatever the
// light and checker make of it.
bool IsLitIn(std::span<const std::uint8_t> rgba, std::size_t channel) {
  for (std::size_t i = 0; i < 3; ++i) {
    if ((i == channel) != (rgba[i] > kClearChannel + kTolerance) || (i != channel && rgba[i] != 0)) {
      return false;
    }
  }
  return rgba[3] == kOpaque;
}

// The crosshair's grey, 0.9 in linear light, as an sRGB byte.
constexpr int kCrosshairChannel = 243;

bool IsCrosshairColor(std::span<const std::uint8_t> rgba) {
  const auto near_crosshair = [](std::uint8_t channel) { return std::abs(channel - kCrosshairChannel) <= kTolerance; };
  return near_crosshair(rgba[0]) && near_crosshair(rgba[1]) && near_crosshair(rgba[2]) && rgba[3] == kOpaque;
}

constexpr std::size_t kRed = 0;
constexpr std::size_t kGreen = 1;
constexpr std::size_t kBlue = 2;

std::span<const std::uint8_t> Pixel(const CapturedFrame& frame, std::uint32_t x, std::uint32_t y) {
  return std::span(frame.pixels).subspan(((std::size_t{y} * frame.width) + x) * kBytesPerPixel, kBytesPerPixel);
}

// Looking down -Z at the plane z = 0 from 5 m away, which then spans about
// +-3.2 m across the frame and +-2.4 m up it, centred on the origin.
const Camera kCamera{.position = {0.0F, 0.0F, 5.0F}, .rotation = {1.0F, 0.0F, 0.0F, 0.0F}, .vertical_fov = 0.9F};

// A rectangle in the plane z = 0, facing the camera.
SceneMesh Rectangle(float left, float bottom, float right, float top, math::Vec3 color) {
  return {.positions = {{left, bottom, 0.0F}, {right, bottom, 0.0F}, {right, top, 0.0F}, {left, top, 0.0F}},
          .indices = {0, 1, 2, 0, 2, 3},
          .color = color};
}

// Where the frame shows the origin, a player standing 1.7 m to its left and
// the scene's blue rectangle 1.7 m to its right; and a corner nothing covers.
constexpr std::uint32_t kCentreX = kWidth / 2;
constexpr std::uint32_t kCentreY = kHeight / 2;
constexpr std::uint32_t kLeftX = 76;
constexpr std::uint32_t kRightX = 244;
constexpr std::uint32_t kCorner = 4;

// A pixel halfway along each of the crosshair's bars, which start 4 pixels off
// the frame's centre and run 8 on: the one after the centre's row or column,
// and the one before it.
constexpr std::uint32_t kAfterCentreToBar = 8;
constexpr std::uint32_t kBeforeCentreToBar = 9;

constexpr std::string_view kCharacter = "characters/soldier";

// A 1 m square character, its origin at the middle of its bottom edge.
SceneMesh CharacterMesh() { return Rectangle(-0.5F, 0.0F, 0.5F, 1.0F, {1.0F, 1.0F, 1.0F}); }

// A player whose square covers the frame at x, half a metre down from the
// origin so that it is centred on it.
RemotePlayer PlayerAt(float x, math::Vec3 color, std::string_view character = kCharacter) {
  return {.position = {x, -0.5F, 0.0F}, .yaw = 0.0F, .color = color, .character = std::string(character)};
}

// A window that never shows, for a test that only reads frames back.
window::Config HiddenWindowConfig() {
  return {.title = "augusta renderer smoke test", .width = kWidth, .height = kHeight, .hidden = true};
}

// What CI's runner, which has no GPU, can run, with every misuse of Vulkan an
// error the Renderer throws.
Config SoftwareConfig() { return {.adapter = Adapter::kSoftware, .validation = Validation::kRequired}; }

class RendererSmokeTest : public ::testing::Test {
 protected:
  // The scene's blue rectangle, seen from its own default camera until
  // SetCamera moves to kCamera; and the character's mesh.
  void SetWorld() {
    renderer_->SetScene({.meshes = {Rectangle(1.2F, -0.5F, 2.2F, 0.5F, {0.0F, 0.0F, 1.0F})}, .camera = {}});
    renderer_->SetCamera(kCamera);
    renderer_->SetCharacterMesh(std::string(kCharacter), CharacterMesh());
  }

  // More frames than are in flight, so that every region of the per-frame
  // upload buffers and every swapchain image comes round, each with players,
  // effects and overlay.
  CapturedFrame RenderFrames(std::span<const RemotePlayer> players = {}, const CombatEffects& effects = {},
                             const Overlay& overlay = {}) {
    for (int i = 0; i < 4; ++i) {
      static_cast<void>(window_.Poll());
      renderer_->SetRemotePlayers(players);
      renderer_->SetCombatEffects(effects);
      renderer_->SetOverlay(overlay);
      renderer_->RenderFrame();
    }
    return renderer_->CaptureFrame();
  }

  // Declared first, so that it outlives the Renderer drawing into it.
  window::Window window_{HiddenWindowConfig()};
  std::unique_ptr<Renderer> renderer_ = std::make_unique<Renderer>(window_, SoftwareConfig());
};

TEST_F(RendererSmokeTest, OpensItsWindowAtTheConfiguredSize) {
  const window::Size size = window_.GetSize();

  EXPECT_EQ(size.width, kWidth);
  EXPECT_EQ(size.height, kHeight);
}

TEST_F(RendererSmokeTest, CapturesNothingBeforeTheFirstFrame) {
  const CapturedFrame frame = renderer_->CaptureFrame();

  EXPECT_EQ(frame.width, 0U);
  EXPECT_EQ(frame.height, 0U);
  EXPECT_TRUE(frame.pixels.empty());
}

// The debug HUD is in a Development build only (ADR-0074).
#if AUGUSTA_DEBUG_TOOLS
TEST_F(RendererSmokeTest, ClearsEveryPixelBelowTheDebugHudToTheClearColor) {
  renderer_->SetTextLines({"FPS: 144 (6.9ms) | RTT: --"});
  RenderFrames();

  const CapturedFrame frame = renderer_->CaptureFrame();

  ASSERT_EQ(frame.width, kWidth);
  ASSERT_EQ(frame.height, kHeight);
  ASSERT_EQ(frame.pixels.size(), std::size_t{kWidth} * kHeight * kBytesPerPixel);
  EXPECT_EQ(CountOtherPixels(frame, kHudBottom, kHeight), 0U);
}
#endif

TEST_F(RendererSmokeTest, DrawsNoDebugHudBeforeItHasLines) {
  const CapturedFrame frame = RenderFrames();

  ASSERT_EQ(frame.height, kHeight);
  EXPECT_EQ(CountOtherPixels(frame, 0, kHeight), 0U);
}

#if AUGUSTA_DEBUG_TOOLS
TEST_F(RendererSmokeTest, DrawsTheDebugHudsLinesInItsRegion) {
  renderer_->SetTextLines({"FPS: --"});
  const std::size_t short_line = CountOtherPixels(RenderFrames(), 0, kHudBottom);

  renderer_->SetTextLines({"FPS: -- | RTT: 12ms | Jitter: 0.4ms | Loss: 0.0% | In: 1.0KB/s | Out: 512B/s"});
  const CapturedFrame frame = RenderFrames();

  ASSERT_EQ(frame.height, kHeight);
  EXPECT_GT(short_line, 0U);
  EXPECT_GT(CountOtherPixels(frame, 0, kHudBottom), short_line);
}
#endif

TEST_F(RendererSmokeTest, DrawsTheSceneAndARemotePlayerFromTheCamera) {
  SetWorld();
  const std::vector<RemotePlayer> players{PlayerAt(0.0F, {1.0F, 0.0F, 0.0F})};

  const CapturedFrame frame = RenderFrames(players);

  ASSERT_EQ(frame.width, kWidth);
  EXPECT_TRUE(IsLitIn(Pixel(frame, kCentreX, kCentreY), kRed));
  EXPECT_TRUE(IsLitIn(Pixel(frame, kRightX, kCentreY), kBlue));
  EXPECT_TRUE(IsClearColor(Pixel(frame, kCorner, kCorner)));
  EXPECT_TRUE(IsClearColor(Pixel(frame, kLeftX, kCentreY)));
}

TEST_F(RendererSmokeTest, HidesAPlayerBehindTheScene) {
  SetWorld();
  RemotePlayer behind = PlayerAt(1.7F, {1.0F, 0.0F, 0.0F});
  behind.position.z = -1.0F;
  const std::vector<RemotePlayer> players{behind};

  const CapturedFrame frame = RenderFrames(players);

  ASSERT_EQ(frame.width, kWidth);
  EXPECT_TRUE(IsLitIn(Pixel(frame, kRightX, kCentreY), kBlue));
}

TEST_F(RendererSmokeTest, DrawsEveryPlayerOnceMoreArriveThanEverBefore) {
  SetWorld();
  const std::vector<RemotePlayer> one{PlayerAt(0.0F, {1.0F, 0.0F, 0.0F})};
  const std::vector<RemotePlayer> two{PlayerAt(0.0F, {1.0F, 0.0F, 0.0F}), PlayerAt(-1.7F, {0.0F, 1.0F, 0.0F})};
  RenderFrames(one);

  const CapturedFrame frame = RenderFrames(two);

  ASSERT_EQ(frame.width, kWidth);
  EXPECT_TRUE(IsLitIn(Pixel(frame, kCentreX, kCentreY), kRed));
  EXPECT_TRUE(IsLitIn(Pixel(frame, kLeftX, kCentreY), kGreen));
}

TEST_F(RendererSmokeTest, DrawsNoPlayersForAnEmptySpan) {
  SetWorld();
  const std::vector<RemotePlayer> players{PlayerAt(0.0F, {1.0F, 0.0F, 0.0F})};
  RenderFrames(players);

  const CapturedFrame frame = RenderFrames({});

  ASSERT_EQ(frame.width, kWidth);
  EXPECT_TRUE(IsClearColor(Pixel(frame, kCentreX, kCentreY)));
  EXPECT_TRUE(IsLitIn(Pixel(frame, kRightX, kCentreY), kBlue));
}

TEST_F(RendererSmokeTest, SkipsAPlayerWhoseCharacterHasNoMesh) {
  SetWorld();
  const std::vector<RemotePlayer> players{PlayerAt(0.0F, {1.0F, 0.0F, 0.0F}, "characters/unknown"),
                                          PlayerAt(-1.7F, {0.0F, 1.0F, 0.0F})};

  const CapturedFrame frame = RenderFrames(players);

  ASSERT_EQ(frame.width, kWidth);
  EXPECT_TRUE(IsClearColor(Pixel(frame, kCentreX, kCentreY)));
  EXPECT_TRUE(IsLitIn(Pixel(frame, kLeftX, kCentreY), kGreen));
}

TEST_F(RendererSmokeTest, AddsATracersGlowToTheSceneBehindIt) {
  SetWorld();
  const CapturedFrame without = RenderFrames();
  const std::span<const std::uint8_t> scene = Pixel(without, kRightX, kCentreY);
  // Across the blue rectangle, a little in front of it, its head past it.
  const CombatEffects effects{
      .tracers = {{.head = {2.6F, 0.0F, 0.1F}, .tail = {0.6F, 0.0F, 0.1F}}}, .impacts = {}, .muzzle_flashes = {}};

  const CapturedFrame frame = RenderFrames({}, effects);

  ASSERT_EQ(frame.width, kWidth);
  const std::span<const std::uint8_t> lit = Pixel(frame, kRightX, kCentreY);
  // The tracer's warm light, where the rectangle had none, on top of the
  // rectangle's blue rather than in place of it.
  EXPECT_GT(lit[kRed], scene[kRed]);
  EXPECT_GT(lit[kGreen], scene[kGreen]);
  EXPECT_GT(lit[kBlue], scene[kBlue]);
  EXPECT_EQ(lit[3], kOpaque);
}

TEST_F(RendererSmokeTest, DrawsTheCrosshairOverAPlayerAtTheFramesCentre) {
  SetWorld();
  const std::vector<RemotePlayer> players{PlayerAt(0.0F, {1.0F, 0.0F, 0.0F})};

  const CapturedFrame frame = RenderFrames(players, {}, {.crosshair = true, .hit_marker = false});

  ASSERT_EQ(frame.width, kWidth);
  // Its bars, written over the player in their own colour, around a gap that
  // still shows the player.
  for (const auto [x, y] :
       {std::pair{kCentreX + kAfterCentreToBar, kCentreY}, std::pair{kCentreX - kBeforeCentreToBar, kCentreY},
        std::pair{kCentreX, kCentreY - kBeforeCentreToBar}, std::pair{kCentreX, kCentreY + kAfterCentreToBar}}) {
    EXPECT_TRUE(IsCrosshairColor(Pixel(frame, x, y))) << x << "," << y;
  }
  EXPECT_TRUE(IsLitIn(Pixel(frame, kCentreX, kCentreY), kRed));
}

TEST_F(RendererSmokeTest, ThrowsOnAnOutOfRangeMeshIndex) {
  SceneMesh mesh = CharacterMesh();
  mesh.indices.back() = 4;

  EXPECT_THROW(renderer_->SetScene({.meshes = {mesh}, .camera = kCamera}), std::runtime_error);
  EXPECT_THROW(renderer_->SetCharacterMesh(std::string(kCharacter), mesh), std::runtime_error);
}

TEST_F(RendererSmokeTest, TearsDownSoThatAnotherRendererCanStart) {
  renderer_->RenderFrame();
  renderer_.reset();

  renderer_ = std::make_unique<Renderer>(window_, SoftwareConfig());
  renderer_->RenderFrame();

  EXPECT_EQ(renderer_->CaptureFrame().width, kWidth);
}

}  // namespace
}  // namespace augusta::renderer
