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

#ifdef _WIN32
#include <windows.h>
#else
#include <fstream>
#include <iterator>

#include <sys/utsname.h>
#endif

#include "proc_files.h"

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

#ifdef _WIN32

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

Memory QueryMemory() {
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status) == 0) {
    return {};
  }
  return {.total_bytes = status.ullTotalPhys, .available_bytes = status.ullAvailPhys};
}

#else

// Empty if path can't be read.
std::string ReadFile(const char* path) {
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

#endif

}  // namespace

std::string CpuModelFromCpuinfo(std::string_view cpuinfo) {
  return std::string(ValueOf(cpuinfo, "model name", ':').value_or(""));
}

Memory MemoryFromMeminfo(std::string_view meminfo) {
  return {.total_bytes = KibInBytes(ValueOf(meminfo, "MemTotal", ':')),
          .available_bytes = KibInBytes(ValueOf(meminfo, "MemAvailable", ':'))};
}

std::string PrettyNameFromOsRelease(std::string_view os_release) {
  std::string_view name = ValueOf(os_release, "PRETTY_NAME", '=').value_or("");
  if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'') && name.back() == name.front()) {
    name = name.substr(1, name.size() - 2);
  }
  return std::string(name);
}

Machine QueryMachine() {
  return {.os = QueryOs(),
          .cpu = {.model = QueryCpuModel(), .logical_cores = std::thread::hardware_concurrency()},
          .memory = QueryMemory()};
}

std::string DescribeMachine(const Machine& machine) {
  return std::format("os={} cpu={} logical_cores={} memory_mib={} memory_available_mib={}", Quoted(machine.os),
                     Quoted(machine.cpu.model),
                     machine.cpu.logical_cores == 0 ? "unknown" : std::to_string(machine.cpu.logical_cores),
                     Mib(machine.memory.total_bytes), Mib(machine.memory.available_bytes));
}

}  // namespace augusta::hardware
