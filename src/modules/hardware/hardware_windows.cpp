#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <windows.h>

#include "augusta/hardware.h"
#include "hardware_platform.h"
// After <windows.h>, whose types they use.
#include <dxgi.h>
#include <wrl/client.h>

namespace augusta::hardware {

namespace {

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

}  // namespace

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

Limits QueryLimits() { return {}; }

// Each hardware adapter DXGI lists, the software rasterizer left out: its
// name, and its user-mode driver's version as Device Manager shows it
// (32.0.15.6094). DXGI names no driver.
std::vector<Gpu> QueryGpus() {
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

}  // namespace augusta::hardware
