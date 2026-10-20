#ifndef AUGUSTA_RENDERER_VULKAN_DEVICE_H_
#define AUGUSTA_RENDERER_VULKAN_DEVICE_H_

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.hpp>

#include "augusta/renderer.h"
#include "augusta/window.h"

/// \file
/// The Renderer's Vulkan device (ADR-0059), private to the module: Vulkan
/// reached through the system's loader, which the client never links or ships,
/// a Vulkan 1.3 instance with or without the validation layer, the window's
/// surface, the physical device the Config's Adapter asks for (decided by
/// device_choice.h) with its graphics queue, watched for GPU crash dumps
/// (gpu_crash_dumps.h), and the NVRHI device over them.
/// renderer.cpp draws through it, and swapchain.h presents with it. Made and
/// used on the Main/Render thread.
namespace augusta::renderer {

class VulkanDevice final {
 public:
  /// Throws std::runtime_error if there is no Vulkan 1.3 loader, if validation
  /// requires the validation layer and the loader finds none, or if no device
  /// is the one adapter asks for. window must outlive this device.
  VulkanDevice(const window::Window& window, Adapter adapter, Validation validation);

  /// Waits for the GPU to finish all it was given.
  ~VulkanDevice();

  VulkanDevice(const VulkanDevice&) = delete;
  VulkanDevice& operator=(const VulkanDevice&) = delete;
  VulkanDevice(VulkanDevice&&) = delete;
  VulkanDevice& operator=(VulkanDevice&&) = delete;

  [[nodiscard]] nvrhi::vulkan::IDevice& Nvrhi() const { return *nvrhi_; }
  [[nodiscard]] vk::PhysicalDevice PhysicalDevice() const { return physical_device_; }
  [[nodiscard]] vk::Device Device() const { return *device_; }
  /// The queue that draws and presents: NVRHI's graphics queue.
  [[nodiscard]] vk::Queue Queue() const { return queue_; }
  [[nodiscard]] vk::SurfaceKHR Surface() const { return *surface_; }

  /// Throws std::runtime_error, with its message, if the validation layer has
  /// reported an error since the last call; does nothing without validation.
  void ThrowValidationErrors();

 private:
  // What NVRHI reports, logged at its own severity.
  class NvrhiMessages final : public nvrhi::IMessageCallback {
   public:
    void message(nvrhi::MessageSeverity severity, const char* text) override;
  };

  // The validation layer's report: logged as a warning, and an error kept for
  // ThrowValidationErrors, since an exception must not unwind through the
  // Vulkan call the layer reports from. user_data is the VulkanDevice.
  static VKAPI_ATTR vk::Bool32 VKAPI_CALL OnValidationMessage(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                              vk::DebugUtilsMessageTypeFlagsEXT types,
                                                              const vk::DebugUtilsMessengerCallbackDataEXT* data,
                                                              void* user_data);

  void CreateInstance(const window::Window& window, Validation requested);
  void ChoosePhysicalDevice(Adapter adapter);
  // gpu_crash_dumps: whether Aftermath is watching, so the device turns on what
  // it reads.
  void CreateDevice(bool gpu_crash_dumps);

  // The first validation error since the last ThrowValidationErrors. Before
  // the messenger, which reports into it.
  std::mutex validation_mutex_;
  std::optional<std::string> validation_error_;

  // NVRHI is told which extensions are on, to know what it may use.
  std::vector<const char*> instance_extensions_;
  std::vector<const char*> device_extensions_;
  vk::UniqueInstance instance_;
  vk::UniqueDebugUtilsMessengerEXT messenger_;
  vk::UniqueSurfaceKHR surface_;
  vk::PhysicalDevice physical_device_;
  std::uint32_t queue_family_ = 0;
  vk::UniqueDevice device_;
  vk::Queue queue_;
  NvrhiMessages messages_;
  nvrhi::vulkan::DeviceHandle nvrhi_;
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_VULKAN_DEVICE_H_
