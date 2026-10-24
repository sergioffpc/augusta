#ifndef AUGUSTA_TESTS_RENDERER_SMOKE_TEST_H_
#define AUGUSTA_TESTS_RENDERER_SMOKE_TEST_H_

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>

#include <gtest/gtest.h>

#include "augusta/renderer.h"
#include "augusta/window.h"

// What the Renderer's smoke tests share: a Renderer on lavapipe in a hidden
// window, and reading back the frames it draws. The debug HUD's cases, in a
// Development build only (ADR-0074), sit in a file of their own.
namespace augusta::renderer::smoke_test {

// Small, so lavapipe draws it fast, but wider than Windows' narrowest window with a
// title bar.
inline constexpr std::uint32_t kWidth = 320;
inline constexpr std::uint32_t kHeight = 240;

// The Renderer's near-black clear color, 0.016 in linear light, as an sRGB
// byte, give or take the GPU's rounding.
inline constexpr int kClearChannel = 34;
inline constexpr int kTolerance = 1;
inline constexpr std::uint8_t kOpaque = 255;

inline constexpr std::size_t kBytesPerPixel = 4;

inline bool IsClearColor(std::span<const std::uint8_t> rgba) {
  const auto near_clear = [](std::uint8_t channel) { return std::abs(channel - kClearChannel) <= kTolerance; };
  return near_clear(rgba[0]) && near_clear(rgba[1]) && near_clear(rgba[2]) && rgba[3] == kOpaque;
}

// How many of frame's pixels in rows [first_row, last_row) are not the clear
// color.
inline std::size_t CountOtherPixels(const CapturedFrame& frame, std::uint32_t first_row, std::uint32_t last_row) {
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

// A window that never shows, for a test that only reads frames back.
inline window::Config HiddenWindowConfig() {
  return {.title = "augusta renderer smoke test", .width = kWidth, .height = kHeight, .hidden = true};
}

// What CI's runner, which has no GPU, can run, with every misuse of Vulkan an
// error the Renderer throws.
inline Config SoftwareConfig() { return {.adapter = Adapter::kSoftware, .validation = Validation::kRequired}; }

class RendererSmokeTest : public ::testing::Test {
 protected:
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

}  // namespace augusta::renderer::smoke_test

#endif  // AUGUSTA_TESTS_RENDERER_SMOKE_TEST_H_
