#include "augusta/hardware.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "proc_files.h"

#ifdef _WIN32
#include <utility>

#include <windows.h>
// After <windows.h>, whose types they use.
#include <dxgi.h>
#include <wrl/client.h>
#elifdef __APPLE__
#include <mach/host_info.h>
#include <mach/kern_return.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <mach/message.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>
#else
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include <sys/utsname.h>
#endif

namespace augusta::hardware {

namespace {

constexpr std::uint64_t kBytesPerKib = 1024;
constexpr std::uint64_t kBytesPerMib = kBytesPerKib * kBytesPerKib;

std::string_view Trim(std::string_view text) {
  constexpr std::string_view kSpace = " \t\r\n";
  const std::size_t first = text.find_first_not_of(kSpace);
  if (first == std::string_view::npos) {
    return {};
  }
  return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
}

// The value on the first line of text naming key, before separator, trimmed;
// nullopt if no line does. Only blanks may come between key and separator, so
// "model" is not read from a "model name" line.
std::optional<std::string_view> ValueOf(std::string_view text, std::string_view key, char separator) {
  while (!text.empty()) {
    const std::size_t end = text.find('\n');
    const std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    if (!line.starts_with(key)) {
      continue;
    }
    const std::string_view rest = line.substr(key.size());
    const std::size_t at = rest.find(separator);
    if (at != std::string_view::npos && Trim(rest.substr(0, at)).empty()) {
      return Trim(rest.substr(at + 1));
    }
  }
  return std::nullopt;
}

// A meminfo value, such as "32768 kB", in bytes; 0 if value is not one.
std::uint64_t KibInBytes(std::optional<std::string_view> value) {
  std::uint64_t kib = 0;
  if (!value || std::from_chars(value->data(), value->data() + value->size(), kib).ec != std::errc{}) {
    return 0;
  }
  return kib * kBytesPerKib;
}

// What a log line shows for text, quoted, or for an empty one.
std::string Quoted(std::string_view text) { return text.empty() ? "unknown" : std::format("\"{}\"", text); }

std::string Mib(std::uint64_t bytes) { return bytes == 0 ? "unknown" : std::to_string(bytes / kBytesPerMib); }

std::string LimitMib(std::optional<std::uint64_t> bytes) {
  return bytes ? std::to_string(*bytes / kBytesPerMib) : "none";
}

// text, in hexadecimal and whole, as a number; nullopt if it is not one.
std::optional<std::uint32_t> HexNumber(std::string_view text) {
  constexpr int kHexadecimal = 16;
  std::uint32_t number = 0;
  const char* const end = text.data() + text.size();
  const auto [last, error] = std::from_chars(text.data(), end, number, kHexadecimal);
  if (text.empty() || error != std::errc{} || last != end) {
    return std::nullopt;
  }
  return number;
}

std::optional<std::uint64_t> Number(std::string_view text) {
  std::uint64_t number = 0;
  const char* const end = text.data() + text.size();
  const auto [last, error] = std::from_chars(text.data(), end, number);
  if (error != std::errc{} || last != end) {
    return std::nullopt;
  }
  return number;
}

// A vendor's name from its PCI ID, or the ID in hexadecimal for another.
std::string VendorName(std::uint32_t vendor_id) {
  constexpr std::uint32_t kNvidia = 0x10de;
  constexpr std::uint32_t kAmd = 0x1002;
  constexpr std::uint32_t kIntel = 0x8086;
  switch (vendor_id) {
    case kNvidia:
      return "NVIDIA";
    case kAmd:
      return "AMD";
    case kIntel:
      return "Intel";
    default:
      return std::format("{:#06x}", vendor_id);
  }
}

#ifdef _WIN32

// text, UTF-16, in UTF-8; empty if it cannot be converted.
std::string Utf8(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }
  const int size =
      WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string converted(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), converted.data(), size, nullptr, nullptr);
  return converted;
}

// A string under HKEY_LOCAL_MACHINE; empty if there is none.
std::string RegistryString(const char* key, const char* value) {
  DWORD size = 0;
  if (RegGetValueA(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS) {
    return {};
  }
  std::string text(size, '\0');
  if (RegGetValueA(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, text.data(), &size) != ERROR_SUCCESS) {
    return {};
  }
  // size counts the terminating null.
  text.resize(size > 0 ? size - 1 : 0);
  return text;
}

std::optional<DWORD> RegistryDword(const char* key, const char* value) {
  DWORD number = 0;
  DWORD size = sizeof(number);
  if (RegGetValueA(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_DWORD, nullptr, &number, &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  return number;
}

// From the registry rather than GetVersionEx, which reports the version the
// executable's manifest declares, not the one running.
std::string QueryOs() {
  constexpr const char* kKey = R"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)";
  const std::optional<DWORD> major = RegistryDword(kKey, "CurrentMajorVersionNumber");
  const std::optional<DWORD> minor = RegistryDword(kKey, "CurrentMinorVersionNumber");
  const std::string build = RegistryString(kKey, "CurrentBuildNumber");
  if (!major || !minor || build.empty()) {
    return {};
  }
  std::string os = std::format("Windows {}.{}.{}", *major, *minor, build);
  if (const std::string release = RegistryString(kKey, "DisplayVersion"); !release.empty()) {
    os += std::format(" ({})", release);
  }
  return os;
}

std::string QueryCpuModel() {
  return std::string(Trim(RegistryString(R"(HARDWARE\DESCRIPTION\System\CentralProcessor\0)", "ProcessorNameString")));
}

Limits QueryLimits() { return {}; }

Memory QueryMemory() {
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status) == 0) {
    return {};
  }
  return {.total_bytes = status.ullTotalPhys, .available_bytes = status.ullAvailPhys};
}

// Each hardware adapter DXGI lists, the software rasterizer left out: its
// name, and its user-mode driver's version as Device Manager shows it
// (32.0.15.6094). DXGI names no driver.
std::vector<Gpu> ListGpus() {
  std::vector<Gpu> gpus;
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return gpus;
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
    DXGI_ADAPTER_DESC1 description{};
    if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
      continue;
    }
    std::string version;
    if (LARGE_INTEGER umd{}; SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
      const auto high = static_cast<std::uint32_t>(umd.HighPart);
      const auto low = static_cast<std::uint32_t>(umd.LowPart);
      version = std::format("{}.{}.{}.{}", HIWORD(high), LOWORD(high), HIWORD(low), LOWORD(low));
    }
    gpus.push_back({.name = Utf8(description.Description),
                    .vendor_id = description.VendorId,
                    .device_id = description.DeviceId,
                    .driver = {},
                    .driver_version = std::move(version)});
  }
  return gpus;
}

#elifdef __APPLE__

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
std::vector<Gpu> ListGpus() { return {}; }

#else

// Empty if path can't be read.
std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

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
std::vector<Gpu> ListGpus() {
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

#endif

}  // namespace

std::string CpuModelFromCpuinfo(std::string_view cpuinfo) {
  return std::string(ValueOf(cpuinfo, "model name", ':').value_or(""));
}

Memory MemoryFromMeminfo(std::string_view meminfo) {
  return {.total_bytes = KibInBytes(ValueOf(meminfo, "MemTotal", ':')),
          .available_bytes = KibInBytes(ValueOf(meminfo, "MemAvailable", ':'))};
}

std::optional<double> CpusFromCpuMax(std::string_view cpu_max) {
  cpu_max = Trim(cpu_max);
  const std::size_t space = cpu_max.find(' ');
  if (space == std::string_view::npos) {
    return std::nullopt;
  }
  const std::optional<std::uint64_t> quota = Number(cpu_max.substr(0, space));
  const std::optional<std::uint64_t> period = Number(Trim(cpu_max.substr(space + 1)));
  if (!quota || !period || *period == 0) {
    return std::nullopt;
  }
  return static_cast<double>(*quota) / static_cast<double>(*period);
}

std::optional<std::uint64_t> BytesFromMemoryMax(std::string_view memory_max) { return Number(Trim(memory_max)); }

std::string PrettyNameFromOsRelease(std::string_view os_release) {
  std::string_view name = ValueOf(os_release, "PRETTY_NAME", '=').value_or("");
  if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'') && name.back() == name.front()) {
    name = name.substr(1, name.size() - 2);
  }
  return std::string(name);
}

std::optional<Gpu> GpuFromUevent(std::string_view uevent) {
  const std::string_view pci_id = ValueOf(uevent, "PCI_ID", '=').value_or("");
  const std::size_t colon = pci_id.find(':');
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }
  const std::optional<std::uint32_t> vendor_id = HexNumber(pci_id.substr(0, colon));
  const std::optional<std::uint32_t> device_id = HexNumber(pci_id.substr(colon + 1));
  if (!vendor_id || !device_id) {
    return std::nullopt;
  }
  return Gpu{.name = {},
             .vendor_id = *vendor_id,
             .device_id = *device_id,
             .driver = std::string(ValueOf(uevent, "DRIVER", '=').value_or("")),
             .driver_version = {}};
}

Machine QueryMachine() {
  return {.os = QueryOs(),
          .cpu = {.model = QueryCpuModel(), .logical_cores = std::thread::hardware_concurrency()},
          .memory = QueryMemory(),
          .limits = QueryLimits()};
}

std::string DescribeMachine(const Machine& machine) {
  return std::format(
      "os={} cpu={} logical_cores={} memory_mib={} memory_available_mib={} cpu_limit={} memory_limit_mib={}",
      Quoted(machine.os), Quoted(machine.cpu.model),
      machine.cpu.logical_cores == 0 ? "unknown" : std::to_string(machine.cpu.logical_cores),
      Mib(machine.memory.total_bytes), Mib(machine.memory.available_bytes),
      machine.limits.cpus ? std::format("{}", *machine.limits.cpus) : "none", LimitMib(machine.limits.memory_bytes));
}

std::vector<Gpu> QueryGpus() { return ListGpus(); }

std::string DescribeGpu(const Gpu& gpu) {
  return std::format("name={} vendor={} device_id={:#06x} driver={} driver_version={}", Quoted(gpu.name),
                     VendorName(gpu.vendor_id), gpu.device_id, Quoted(gpu.driver), Quoted(gpu.driver_version));
}

}  // namespace augusta::hardware
