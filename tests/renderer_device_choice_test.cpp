#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/renderer.h"
#include "device_choice.h"

namespace augusta::renderer {
namespace {

DeviceCandidate Usable(DeviceType type) { return {.type = type, .supports_vulkan_1_3 = true, .presents = true}; }

TEST(DeviceChoiceTest, HardwarePrefersADiscreteGpuOverAnIntegratedOne) {
  const std::vector<DeviceCandidate> candidates{Usable(DeviceType::kIntegratedGpu), Usable(DeviceType::kDiscreteGpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kHardware), std::optional<std::size_t>{1});
}

TEST(DeviceChoiceTest, HardwareTakesAnIntegratedGpuWithoutADiscreteOne) {
  const std::vector<DeviceCandidate> candidates{Usable(DeviceType::kCpu), Usable(DeviceType::kIntegratedGpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kHardware), std::optional<std::size_t>{1});
}

TEST(DeviceChoiceTest, HardwareNeverTakesACpuDevice) {
  const std::vector<DeviceCandidate> candidates{Usable(DeviceType::kCpu), Usable(DeviceType::kOther)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kHardware), std::nullopt);
}

TEST(DeviceChoiceTest, SoftwareTakesTheFirstCpuDevice) {
  const std::vector<DeviceCandidate> candidates{Usable(DeviceType::kDiscreteGpu), Usable(DeviceType::kCpu),
                                                Usable(DeviceType::kCpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kSoftware), std::optional<std::size_t>{1});
}

TEST(DeviceChoiceTest, SoftwareNeverFallsBackToAGpu) {
  const std::vector<DeviceCandidate> candidates{Usable(DeviceType::kDiscreteGpu), Usable(DeviceType::kIntegratedGpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kSoftware), std::nullopt);
}

TEST(DeviceChoiceTest, PassesOverADeviceWithoutVulkan13) {
  DeviceCandidate old_discrete = Usable(DeviceType::kDiscreteGpu);
  old_discrete.supports_vulkan_1_3 = false;
  const std::vector<DeviceCandidate> candidates{old_discrete, Usable(DeviceType::kIntegratedGpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kHardware), std::optional<std::size_t>{1});
}

TEST(DeviceChoiceTest, PassesOverADeviceThatCannotPresentToTheWindow) {
  DeviceCandidate headless = Usable(DeviceType::kCpu);
  headless.presents = false;
  const std::vector<DeviceCandidate> candidates{headless, Usable(DeviceType::kCpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kSoftware), std::optional<std::size_t>{1});
}

TEST(DeviceChoiceTest, ChoosesNothingFromNoDevices) {
  EXPECT_EQ(ChooseDevice({}, Adapter::kHardware), std::nullopt);
  EXPECT_EQ(ChooseDevice({}, Adapter::kSoftware), std::nullopt);
}

// What Vulkan names the device extensions the Renderer may ask for, spelt out
// rather than taken from device_choice.h, so a misspelling there fails.
constexpr std::string_view kSwapchain = "VK_KHR_swapchain";
constexpr std::string_view kDiagnosticsConfig = "VK_NV_device_diagnostics_config";
constexpr std::string_view kDiagnosticCheckpoints = "VK_NV_device_diagnostic_checkpoints";

std::vector<std::string_view> Names(const std::vector<const char*>& extensions) {
  return {extensions.begin(), extensions.end()};
}

TEST(DeviceExtensionsTest, WithoutGpuCrashDumpsAsksForTheSwapchainAlone) {
  const std::vector<std::string_view> available{kSwapchain, kDiagnosticsConfig, kDiagnosticCheckpoints};

  EXPECT_EQ(Names(DeviceExtensions(false, available)), std::vector<std::string_view>{kSwapchain});
}

TEST(DeviceExtensionsTest, WithGpuCrashDumpsAlsoAsksForWhatAftermathReads) {
  const std::vector<std::string_view> available{kDiagnosticCheckpoints, kSwapchain, kDiagnosticsConfig};

  EXPECT_EQ(Names(DeviceExtensions(true, available)),
            (std::vector<std::string_view>{kSwapchain, kDiagnosticsConfig, kDiagnosticCheckpoints}));
}

// lavapipe, or a GPU that is not NVIDIA's: Aftermath may watch, but the device
// is made as without it.
TEST(DeviceExtensionsTest, WithGpuCrashDumpsAsksForNoExtensionTheDeviceLacks) {
  const std::vector<std::string_view> available{kSwapchain};

  EXPECT_EQ(Names(DeviceExtensions(true, available)), std::vector<std::string_view>{kSwapchain});
}

}  // namespace
}  // namespace augusta::renderer
