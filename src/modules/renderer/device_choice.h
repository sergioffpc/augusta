#ifndef AUGUSTA_RENDERER_DEVICE_CHOICE_H_
#define AUGUSTA_RENDERER_DEVICE_CHOICE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "augusta/renderer.h"

/// \file
/// Decision half of which Vulkan physical device the Renderer draws with
/// (ADR-0059), private to the module and kept free of any GPU API so it is
/// tested on every platform: vulkan_device.cpp asks Vulkan what each device is
/// and creates the one chosen.
namespace augusta::renderer {

/// What a physical device is, as Vulkan's VkPhysicalDeviceType says.
enum class DeviceType : std::uint8_t {
  kDiscreteGpu,
  kIntegratedGpu,
  /// A software rasterizer, such as Mesa's lavapipe.
  kCpu,
  /// A virtual GPU, or one Vulkan names no type for.
  kOther,
};

/// A physical device, as far as choosing one goes.
struct DeviceCandidate {
  DeviceType type = DeviceType::kOther;
  /// The Renderer's minimum: dynamic rendering and synchronization2 in core.
  bool supports_vulkan_1_3 = false;
  /// Whether one of its queue families both draws and presents to the window.
  bool presents = false;
};

/// The index in candidates of the device adapter asks for, or nullopt if none
/// is one: for kHardware the first discrete GPU, else the first integrated one;
/// for kSoftware the first CPU device. A device without Vulkan 1.3, or which
/// can't present to the window, is passed over.
std::optional<std::size_t> ChooseDevice(std::span<const DeviceCandidate> candidates, Adapter adapter);

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_DEVICE_CHOICE_H_
