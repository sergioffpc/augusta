#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include <mach/host_info.h>
#include <mach/kern_return.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <mach/message.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "augusta/hardware.h"
#include "hardware_platform.h"

namespace augusta::hardware {

namespace {

// A sysctl string by name; empty if there is none.
std::string SysctlString(const char* name) {
  std::size_t size = 0;
  if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) {
    return {};
  }
  std::string text(size, '\0');
  if (sysctlbyname(name, text.data(), &size, nullptr, 0) != 0) {
    return {};
  }
  // size counts the terminating null.
  text.resize(size > 0 ? size - 1 : 0);
  return text;
}

}  // namespace

std::string QueryOs() {
  const std::string version = SysctlString("kern.osproductversion");
  utsname name{};
  if (uname(&name) != 0) {
    return version.empty() ? std::string{} : std::format("macOS {}", version);
  }
  const std::string kernel = std::format("{} {}", name.sysname, name.release);
  return version.empty() ? kernel : std::format("macOS {} ({})", version, kernel);
}

std::string QueryCpuModel() { return std::string(Trim(SysctlString("machdep.cpu.brand_string"))); }

// The physical memory, and what is free or inactive of it: the pages the
// system hands out without paging anything out.
Memory QueryMemory() {
  std::uint64_t total = 0;
  std::size_t size = sizeof(total);
  if (sysctlbyname("hw.memsize", &total, &size, nullptr, 0) != 0) {
    return {};
  }
  vm_statistics64_data_t statistics{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  const auto page_size = sysconf(_SC_PAGESIZE);
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &count) !=
          KERN_SUCCESS ||
      page_size <= 0) {
    return {.total_bytes = total, .available_bytes = 0};
  }
  const std::uint64_t free_pages = std::uint64_t{statistics.free_count} + statistics.inactive_count;
  return {.total_bytes = total, .available_bytes = free_pages * static_cast<std::uint64_t>(page_size)};
}

// macOS has no cgroups.
Limits QueryLimits() { return {}; }

// Not listed: macOS's GPUs are in IOKit's registry, which nothing here reads.
std::vector<Gpu> QueryGpus() { return {}; }

}  // namespace augusta::hardware
