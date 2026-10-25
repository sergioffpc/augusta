#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/utsname.h>

#include "augusta/hardware.h"
#include "hardware_platform.h"
#include "proc_files.h"

namespace augusta::hardware {

namespace {

// Empty if path can't be read.
std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

std::string QueryOs() {
  const std::string distribution = PrettyNameFromOsRelease(ReadFile("/etc/os-release"));
  utsname name{};
  if (uname(&name) != 0) {
    return distribution;
  }
  const std::string kernel = std::format("{} {}", name.sysname, name.release);
  return distribution.empty() ? kernel : std::format("{} ({})", distribution, kernel);
}

std::string QueryCpuModel() { return CpuModelFromCpuinfo(ReadFile("/proc/cpuinfo")); }

Memory QueryMemory() { return MemoryFromMeminfo(ReadFile("/proc/meminfo")); }

// The process's own cgroup, which a container sees at the root of its cgroup
// namespace.
Limits QueryLimits() {
  return {.cpus = CpusFromCpuMax(ReadFile("/sys/fs/cgroup/cpu.max")),
          .memory_bytes = BytesFromMemoryMax(ReadFile("/sys/fs/cgroup/memory.max"))};
}

// Each GPU the kernel's DRM lists as /sys/class/drm/card<n>, in the order it
// numbers them, with its driver module's version where the module gives one
// (an out-of-tree one, as NVIDIA's; an in-tree one is the kernel's).
std::vector<Gpu> QueryGpus() {
  std::vector<std::filesystem::path> cards;
  std::error_code error;
  for (std::filesystem::directory_iterator it("/sys/class/drm", error), end; !error && it != end; it.increment(error)) {
    const std::string name = it->path().filename().string();
    if (name.starts_with("card") && name.size() > 4 && name.find_first_not_of("0123456789", 4) == std::string::npos) {
      cards.push_back(it->path());
    }
  }
  std::ranges::sort(cards);
  std::vector<Gpu> gpus;
  for (const auto& card : cards) {
    if (std::optional<Gpu> gpu = GpuFromUevent(ReadFile(card / "device" / "uevent"))) {
      if (!gpu->driver.empty()) {
        gpu->driver_version =
            std::string(Trim(ReadFile(std::filesystem::path("/sys/module") / gpu->driver / "version")));
      }
      gpus.push_back(*std::move(gpu));
    }
  }
  return gpus;
}

}  // namespace augusta::hardware
