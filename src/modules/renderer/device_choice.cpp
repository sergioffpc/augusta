#include "device_choice.h"

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <span>

#include "augusta/renderer.h"

namespace augusta::renderer {

namespace {

// The first usable candidate of type.
std::optional<std::size_t> FirstOf(std::span<const DeviceCandidate> candidates, DeviceType type) {
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const DeviceCandidate& candidate = candidates[i];
    if (candidate.type == type && candidate.supports_vulkan_1_3 && candidate.presents) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::size_t> ChooseDevice(std::span<const DeviceCandidate> candidates, Adapter adapter) {
  if (adapter == Adapter::kSoftware) {
    return FirstOf(candidates, DeviceType::kCpu);
  }
  for (const DeviceType type : {DeviceType::kDiscreteGpu, DeviceType::kIntegratedGpu}) {
    if (const std::optional<std::size_t> chosen = FirstOf(candidates, type)) {
      return chosen;
    }
  }
  return std::nullopt;
}

}  // namespace augusta::renderer
