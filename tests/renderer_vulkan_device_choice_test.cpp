#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/renderer.h"
#include "vulkan_device_choice.h"

namespace augusta::renderer {
namespace {

DeviceCandidate UsableCandidate(DeviceType type) {
  return {.type = type, .supports_vulkan_1_3 = true, .presents = true};
}

TEST(DeviceChoiceTest, UsableNeedsVulkan13AndPresenting) {
  EXPECT_TRUE(Usable(UsableCandidate(DeviceType::kDiscreteGpu)));
  EXPECT_FALSE(
      Usable(DeviceCandidate{.type = DeviceType::kDiscreteGpu, .supports_vulkan_1_3 = false, .presents = true}));
  EXPECT_FALSE(
      Usable(DeviceCandidate{.type = DeviceType::kDiscreteGpu, .supports_vulkan_1_3 = true, .presents = false}));
}

TEST(DeviceChoiceTest, HardwarePrefersADiscreteGpuOverAnIntegratedOne) {
  const std::vector<DeviceCandidate> candidates{UsableCandidate(DeviceType::kIntegratedGpu),
                                                UsableCandidate(DeviceType::kDiscreteGpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kHardware), std::optional<std::size_t>{1});
}

TEST(DeviceChoiceTest, HardwareTakesAnIntegratedGpuWithoutADiscreteOne) {
  const std::vector<DeviceCandidate> candidates{UsableCandidate(DeviceType::kCpu),
                                                UsableCandidate(DeviceType::kIntegratedGpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kHardware), std::optional<std::size_t>{1});
}

TEST(DeviceChoiceTest, HardwareNeverTakesACpuDevice) {
  const std::vector<DeviceCandidate> candidates{UsableCandidate(DeviceType::kCpu), UsableCandidate(DeviceType::kOther)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kHardware), std::nullopt);
}

TEST(DeviceChoiceTest, SoftwareTakesTheFirstCpuDevice) {
  const std::vector<DeviceCandidate> candidates{UsableCandidate(DeviceType::kDiscreteGpu),
                                                UsableCandidate(DeviceType::kCpu), UsableCandidate(DeviceType::kCpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kSoftware), std::optional<std::size_t>{1});
}

TEST(DeviceChoiceTest, SoftwareNeverFallsBackToAGpu) {
  const std::vector<DeviceCandidate> candidates{UsableCandidate(DeviceType::kDiscreteGpu),
                                                UsableCandidate(DeviceType::kIntegratedGpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kSoftware), std::nullopt);
}

TEST(DeviceChoiceTest, PassesOverADeviceWithoutVulkan13) {
  DeviceCandidate old_discrete = UsableCandidate(DeviceType::kDiscreteGpu);
  old_discrete.supports_vulkan_1_3 = false;
  const std::vector<DeviceCandidate> candidates{old_discrete, UsableCandidate(DeviceType::kIntegratedGpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kHardware), std::optional<std::size_t>{1});
}

TEST(DeviceChoiceTest, PassesOverADeviceThatCannotPresentToTheWindow) {
  DeviceCandidate headless = UsableCandidate(DeviceType::kCpu);
  headless.presents = false;
  const std::vector<DeviceCandidate> candidates{headless, UsableCandidate(DeviceType::kCpu)};

  EXPECT_EQ(ChooseDevice(candidates, Adapter::kSoftware), std::optional<std::size_t>{1});
}

TEST(DeviceChoiceTest, ChoosesNothingFromNoDevices) {
  EXPECT_EQ(ChooseDevice({}, Adapter::kHardware), std::nullopt);
  EXPECT_EQ(ChooseDevice({}, Adapter::kSoftware), std::nullopt);
}

// What Vulkan names the device extensions the Renderer may ask for, spelt out
// rather than taken from vulkan_device_choice.h, so a misspelling there fails.
constexpr std::string_view kSwapchain = "VK_KHR_swapchain";
constexpr std::string_view kDiagnosticsConfig = "VK_NV_device_diagnostics_config";
constexpr std::string_view kDiagnosticCheckpoints = "VK_NV_device_diagnostic_checkpoints";
constexpr std::string_view kPortabilitySubset = "VK_KHR_portability_subset";

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

// MoltenVK's device, on macOS: Vulkan requires the subset turned on wherever a
// device has it.
TEST(DeviceExtensionsTest, AsksForThePortabilitySubsetTheDeviceHas) {
  const std::vector<std::string_view> available{kPortabilitySubset, kSwapchain};

  EXPECT_EQ(Names(DeviceExtensions(false, available)), (std::vector<std::string_view>{kSwapchain, kPortabilitySubset}));
}

}  // namespace
}  // namespace augusta::renderer
