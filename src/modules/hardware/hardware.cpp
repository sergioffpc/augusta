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

#include "hardware_platform.h"
#include "proc_files.h"

namespace augusta::hardware {

namespace {

constexpr std::uint64_t kBytesPerKib = 1024;
constexpr std::uint64_t kBytesPerMib = kBytesPerKib * kBytesPerKib;

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

}  // namespace

std::string_view Trim(std::string_view text) {
  constexpr std::string_view kSpace = " \t\r\n";
  const std::size_t first = text.find_first_not_of(kSpace);
  if (first == std::string_view::npos) {
    return {};
  }
  return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
}

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

std::string DescribeGpu(const Gpu& gpu) {
  return std::format("name={} vendor={} device_id={:#06x} driver={} driver_version={}", Quoted(gpu.name),
                     VendorName(gpu.vendor_id), gpu.device_id, Quoted(gpu.driver), Quoted(gpu.driver_version));
}

std::string DescribeAllocator(std::optional<int> mimalloc_version) {
  if (!mimalloc_version) {
    return "allocator=platform";
  }
  constexpr int kMajor = 100;
  constexpr int kMinor = 10;
  return std::format("allocator=mimalloc/{}.{}.{}", *mimalloc_version / kMajor, *mimalloc_version % kMajor / kMinor,
                     *mimalloc_version % kMinor);
}

}  // namespace augusta::hardware
