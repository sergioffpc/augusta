#ifndef AUGUSTA_RENDERER_VULKAN_SWAPCHAIN_H_
#define AUGUSTA_RENDERER_VULKAN_SWAPCHAIN_H_

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <nvrhi/nvrhi.h>
#include <vulkan/vulkan.hpp>

#include "augusta/window.h"
#include "vulkan_device.h"

/// \file
/// The Renderer's swapchain (ADR-0059), private to the module: the window's
/// images, into which renderer.cpp copies each frame it draws offscreen, and
/// their presentation. Made again when the window's size changes or Vulkan
/// says it no longer fits the window, and absent while the window is minimized.
/// Used on the Main/Render thread.
namespace augusta::renderer {

class VulkanSwapchain final {
 public:
  /// How many frames the Renderer may have in flight: Acquire's regions.
  static constexpr std::uint32_t kFramesInFlight = 3;

  /// Throws std::runtime_error if the window's surface takes no 4-byte sRGB
  /// format. device must outlive this swapchain.
  VulkanSwapchain(VulkanDevice& device, window::Size size);

  VulkanSwapchain(const VulkanSwapchain&) = delete;
  VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
  VulkanSwapchain(VulkanSwapchain&&) = delete;
  VulkanSwapchain& operator=(VulkanSwapchain&&) = delete;

  /// Waits for the GPU and the display to finish with its images.
  ~VulkanSwapchain();

  /// Its images' format, which the Renderer draws in so that a copy into them
  /// keeps every byte: sRGB, 4 bytes a pixel, in red-first or blue-first
  /// order, whichever the surface takes. The same for its whole life.
  [[nodiscard]] nvrhi::Format Format() const { return format_; }

  /// Its images' size; nullopt while it has none, the window being minimized.
  [[nodiscard]] std::optional<window::Size> ImageSize() const;

  /// Makes it again, after waiting for the GPU, if size is not the one it was
  /// made for, if Vulkan said it no longer fits the window, or if it has no
  /// images.
  void Update(window::Size size);

  /// The next image to copy a frame into, using frame region region's
  /// semaphore, which the frame last in that region must be done with; nullptr
  /// if there is none to present this frame. The next command list the device
  /// executes waits for the image, and lets Present go once it is done.
  [[nodiscard]] nvrhi::ITexture* Acquire(std::uint32_t region);

  /// Presents the image Acquire last gave, once the command list executed
  /// after it is done. Throws std::runtime_error if presenting fails, the
  /// device lost included, once its GPU crash dump is written
  /// (vulkan_gpu_crash_dumps.h).
  void Present();

 private:
  void Create();

  VulkanDevice& device_;
  vk::SurfaceFormatKHR surface_format_;
  nvrhi::Format format_ = nvrhi::Format::UNKNOWN;
  vk::PresentModeKHR present_mode_ = vk::PresentModeKHR::eFifo;
  // The size it was last made for, and whether Vulkan has said it no longer
  // fits the window since.
  window::Size size_;
  bool stale_ = false;
  vk::UniqueSwapchainKHR swapchain_;
  vk::Extent2D extent_;
  std::vector<nvrhi::TextureHandle> images_;
  // Signaled once a frame's copy into an image is done, one per image: an
  // image's is waited on by its presentation, which nothing else tracks, and
  // is reused only once that image comes round again.
  std::vector<vk::UniqueSemaphore> copied_;
  // Signaled once an acquired image is ready, one per frame region.
  std::array<vk::UniqueSemaphore, kFramesInFlight> acquired_;
  std::optional<std::uint32_t> image_index_;
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_VULKAN_SWAPCHAIN_H_
