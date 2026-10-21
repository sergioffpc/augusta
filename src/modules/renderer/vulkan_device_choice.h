#ifndef AUGUSTA_RENDERER_VULKAN_DEVICE_CHOICE_H_
#define AUGUSTA_RENDERER_VULKAN_DEVICE_CHOICE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "augusta/renderer.h"

/// \file
/// Decision half of which Vulkan physical device the Renderer draws with, and
/// which of its extensions it turns on (ADR-0059), private to the module and
/// kept free of any GPU API so it is tested on every platform:
/// vulkan_device.cpp asks Vulkan what each device is and has, and creates the
/// one chosen with the extensions chosen.
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

/// Whether candidate could be drawn with at all: it has Vulkan 1.3 and presents
/// to the window.
bool Usable(const DeviceCandidate& candidate);

/// The index in candidates of the device adapter asks for, or nullopt if none
/// is one: for kHardware the first discrete GPU, else the first integrated one;
/// for kSoftware the first CPU device. A device without Vulkan 1.3, or which
/// can't present to the window, is passed over.
std::optional<std::size_t> ChooseDevice(std::span<const DeviceCandidate> candidates, Adapter adapter);

/// What Vulkan names the device extensions the Renderer may turn on.
constexpr const char* kSwapchainExtension = "VK_KHR_swapchain";
constexpr const char* kDiagnosticsConfigExtension = "VK_NV_device_diagnostics_config";
constexpr const char* kDiagnosticCheckpointsExtension = "VK_NV_device_diagnostic_checkpoints";

/// The device extensions that the Renderer turns on for a device that has
/// available: the swapchain's, and, when gpu_crash_dumps says Aftermath is
/// watching (vulkan_gpu_crash_dumps.h), whichever of the two extensions Aftermath
/// reads the device has, diagnostics config and diagnostic checkpoints. Each
/// is one of the names above.
std::vector<const char*> DeviceExtensions(bool gpu_crash_dumps, std::span<const std::string_view> available);

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_VULKAN_DEVICE_CHOICE_H_
