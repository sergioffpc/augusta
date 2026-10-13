#include <cstddef>
#include <optional>
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

}  // namespace
}  // namespace augusta::renderer
