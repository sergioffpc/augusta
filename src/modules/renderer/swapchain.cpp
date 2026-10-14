#include "swapchain.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include <nvrhi/common/resource.h>
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.hpp>

#include "augusta/renderer.h"
#include "vulkan_device.h"

namespace augusta::renderer {

namespace {

struct FormatChoice {
  vk::Format vulkan;
  nvrhi::Format nvrhi;
};

// sRGB, so that shading stays linear, with 4 bytes a pixel, in the two orders
// surfaces take, blue-first being the most common.
constexpr std::array<FormatChoice, 2> kFormats{{
    {.vulkan = vk::Format::eB8G8R8A8Srgb, .nvrhi = nvrhi::Format::SBGRA8_UNORM},
    {.vulkan = vk::Format::eR8G8B8A8Srgb, .nvrhi = nvrhi::Format::SRGBA8_UNORM},
}};

// The first of kFormats surface takes.
std::pair<vk::SurfaceFormatKHR, nvrhi::Format> ChooseFormat(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
  const std::vector<vk::SurfaceFormatKHR> available = device.getSurfaceFormatsKHR(surface);
  for (const FormatChoice& choice : kFormats) {
    const auto found = std::ranges::find_if(available, [&](const vk::SurfaceFormatKHR& format) {
      return format.format == choice.vulkan && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    if (found != available.end()) {
      return {*found, choice.nvrhi};
    }
  }
  throw std::runtime_error("The window's Vulkan surface takes no 4-byte sRGB format");
}

// Never waiting for the display: the latest frame at the next refresh
// (mailbox), else at once (immediate), else FIFO, which every device has.
vk::PresentModeKHR ChoosePresentMode(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
  const std::vector<vk::PresentModeKHR> available = device.getSurfacePresentModesKHR(surface);
  for (const vk::PresentModeKHR mode : {vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eImmediate}) {
    if (std::ranges::contains(available, mode)) {
      return mode;
    }
  }
  return vk::PresentModeKHR::eFifo;
}

// The surface's own size if it has one, which Windows' always does, else size
// within its limits.
vk::Extent2D ExtentFor(const vk::SurfaceCapabilitiesKHR& capabilities, Size size) {
  if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
    return capabilities.currentExtent;
  }
  return {std::clamp(size.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
          std::clamp(size.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
}

// One image more than frames in flight would leave the CPU waiting on the
// display; fewer than the surface's minimum it can't have.
std::uint32_t ImageCountFor(const vk::SurfaceCapabilitiesKHR& capabilities) {
  const std::uint32_t count = std::max(capabilities.minImageCount, Swapchain::kFramesInFlight);
  return capabilities.maxImageCount == 0 ? count : std::min(count, capabilities.maxImageCount);
}

void ThrowIfFailed(VkResult result, const char* what) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::format("{} failed: {}", what, nvrhi::vulkan::resultToString(result)));
  }
}

}  // namespace

Swapchain::Swapchain(VulkanDevice& device, Size size)
    : device_(device), present_mode_(ChoosePresentMode(device.PhysicalDevice(), device.Surface())), size_(size) {
  std::tie(surface_format_, format_) = ChooseFormat(device.PhysicalDevice(), device.Surface());
  for (vk::UniqueSemaphore& semaphore : acquired_) {
    semaphore = device.Device().createSemaphoreUnique({});
  }
  Create();
}

Swapchain::~Swapchain() {
  // Presentation, which the GPU's own tracking leaves out, is done with every
  // semaphore once the device is idle; and the swapchain goes before them.
  device_.Device().waitIdle();
  images_.clear();
  swapchain_.reset();
}

std::optional<Size> Swapchain::ImageSize() const {
  if (!swapchain_) {
    return std::nullopt;
  }
  return Size{.width = extent_.width, .height = extent_.height};
}

void Swapchain::Update(Size size) {
  if (swapchain_ && !stale_ && size.width == size_.width && size.height == size_.height) {
    return;
  }
  size_ = size;
  Create();
}

nvrhi::ITexture* Swapchain::Acquire(std::uint32_t region) {
  image_index_.reset();
  if (!swapchain_) {
    return nullptr;
  }
  const vk::Semaphore acquired = *acquired_.at(region);
  std::uint32_t index = 0;
  const VkResult result = VULKAN_HPP_DEFAULT_DISPATCHER.vkAcquireNextImageKHR(
      device_.Device(), *swapchain_, std::numeric_limits<std::uint64_t>::max(), acquired, VK_NULL_HANDLE, &index);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    stale_ = true;
    return nullptr;
  }
  // Suboptimal still gives an image, to present before the swapchain is made
  // again.
  if (result == VK_SUBOPTIMAL_KHR) {
    stale_ = true;
  } else {
    ThrowIfFailed(result, "Acquiring a swapchain image");
  }
  image_index_ = index;
  device_.Nvrhi().queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, acquired, 0);
  device_.Nvrhi().queueSignalSemaphore(nvrhi::CommandQueue::Graphics, *copied_.at(index), 0);
  return images_.at(index);
}

void Swapchain::Present() {
  if (!image_index_) {
    return;
  }
  const std::uint32_t index = *image_index_;
  image_index_.reset();
  const vk::Semaphore copied = *copied_.at(index);
  const vk::SwapchainKHR swapchain = *swapchain_;
  const vk::PresentInfoKHR info =
      vk::PresentInfoKHR().setWaitSemaphores(copied).setSwapchains(swapchain).setImageIndices(index);
  const VkResult result =
      VULKAN_HPP_DEFAULT_DISPATCHER.vkQueuePresentKHR(device_.Queue(), &static_cast<const VkPresentInfoKHR&>(info));
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    stale_ = true;
    return;
  }
  ThrowIfFailed(result, "Presenting");
}

void Swapchain::Create() {
  const vk::Device device = device_.Device();
  device.waitIdle();
  images_.clear();
  image_index_.reset();
  device_.Nvrhi().runGarbageCollection();
  stale_ = false;

  const vk::SurfaceCapabilitiesKHR capabilities = device_.PhysicalDevice().getSurfaceCapabilitiesKHR(device_.Surface());
  extent_ = ExtentFor(capabilities, size_);
  vk::UniqueSwapchainKHR old = std::move(swapchain_);
  // A minimized window's surface is 0 by 0, which no swapchain can be.
  if (extent_.width != 0 && extent_.height != 0) {
    swapchain_ = device.createSwapchainKHRUnique(vk::SwapchainCreateInfoKHR()
                                                     .setSurface(device_.Surface())
                                                     .setMinImageCount(ImageCountFor(capabilities))
                                                     .setImageFormat(surface_format_.format)
                                                     .setImageColorSpace(surface_format_.colorSpace)
                                                     .setImageExtent(extent_)
                                                     .setImageArrayLayers(1)
                                                     .setImageUsage(vk::ImageUsageFlagBits::eTransferDst)
                                                     .setImageSharingMode(vk::SharingMode::eExclusive)
                                                     .setPreTransform(capabilities.currentTransform)
                                                     .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
                                                     .setPresentMode(present_mode_)
                                                     .setClipped(vk::True)
                                                     .setOldSwapchain(old.get()));
  }
  old.reset();
  copied_.clear();
  if (!swapchain_) {
    return;
  }

  const nvrhi::TextureDesc desc = nvrhi::TextureDesc()
                                      .setWidth(extent_.width)
                                      .setHeight(extent_.height)
                                      .setFormat(format_)
                                      .setInitialState(nvrhi::ResourceStates::Present)
                                      .setKeepInitialState(true)
                                      .setDebugName("Swapchain image");
  for (const vk::Image image : device.getSwapchainImagesKHR(*swapchain_)) {
    images_.push_back(device_.Nvrhi().createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image,
                                                                   nvrhi::Object(static_cast<VkImage>(image)), desc));
    copied_.push_back(device.createSemaphoreUnique({}));
  }
}

}  // namespace augusta::renderer
