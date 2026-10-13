#include "gpu_crash_dumps.h"

#include <d3d12.h>

#if AUGUSTA_HAS_AFTERMATH

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>

#include "augusta/logging.h"
#include "augusta/version.h"

// Aftermath declares its D3D12 functions only once d3d12.h is included.
#include <GFSDK_Aftermath.h>
#include <GFSDK_Aftermath_Defines.h>
#include <GFSDK_Aftermath_GpuCrashDump.h>

#endif

namespace augusta::renderer {

#if AUGUSTA_HAS_AFTERMATH

namespace {

// How long AwaitGpuCrashDump waits for Aftermath, and how often it checks.
constexpr std::chrono::seconds kDumpTimeout{5};
constexpr std::chrono::milliseconds kDumpPoll{50};

// Whether EnableGpuCrashDumps started Aftermath; read by Attach and Await.
std::atomic<bool> crash_dumps_enabled{false};

std::filesystem::path ExecutablePath() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length < path.size()) {
      path.resize(length);
      return path;
    }
    path.resize(path.size() * 2);
  }
}

// Writes a dump beside the executable, named after it, the process and how
// many dumps came before: a fault can be reported more than once.
void WriteDump(const void* dump, std::uint32_t size, void* /*user_data*/) {
  static std::atomic<int> count{0};
  const std::filesystem::path executable = ExecutablePath();
  const std::filesystem::path path =
      executable.parent_path() /
      std::format("{}-{}-{}.nv-gpudmp", executable.stem().string(), GetCurrentProcessId(), ++count);
  std::ofstream file(path, std::ios::binary);
  file.write(static_cast<const char*>(dump), size);
  if (!file) {
    LE("subsystem=renderer event=gpu_crash_dump_failed path=\"{}\"", path.string());
    return;
  }
  LC("subsystem=renderer event=gpu_crash_dump path=\"{}\"", path.string());
}

void DescribeDump(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription add, void* /*user_data*/) {
  add(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, ExecutablePath().stem().string().c_str());
  add(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationVersion, std::string(EngineVersion()).c_str());
}

}  // namespace

void EnableGpuCrashDumps() {
  static std::once_flag once;
  std::call_once(once, [] {
    const GFSDK_Aftermath_Result result = GFSDK_Aftermath_EnableGpuCrashDumps(
        GFSDK_Aftermath_Version_API, GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX,
        GFSDK_Aftermath_GpuCrashDumpFeatureFlags_Default, WriteDump, nullptr, DescribeDump, nullptr, nullptr);
    if (!GFSDK_Aftermath_SUCCEED(result)) {
      LW("subsystem=renderer event=gpu_crash_dumps_unavailable result={:#x}", static_cast<std::uint32_t>(result));
      return;
    }
    crash_dumps_enabled = true;
  });
}

void AttachGpuCrashDumps(ID3D12Device& device) {
  if (!crash_dumps_enabled) {
    return;
  }
  // Call stacks need markers, though the Renderer sets none.
  const GFSDK_Aftermath_Result result = GFSDK_Aftermath_DX12_Initialize(
      GFSDK_Aftermath_Version_API,
      GFSDK_Aftermath_FeatureFlags_EnableMarkers | GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |
          GFSDK_Aftermath_FeatureFlags_CallStackCapturing,
      &device);
  if (!GFSDK_Aftermath_SUCCEED(result)) {
    LW("subsystem=renderer event=gpu_crash_dumps_not_attached result={:#x}", static_cast<std::uint32_t>(result));
    return;
  }
  LI("subsystem=renderer event=gpu_crash_dumps_attached");
}

void AwaitGpuCrashDump() {
  if (!crash_dumps_enabled) {
    return;
  }
  const auto deadline = std::chrono::steady_clock::now() + kDumpTimeout;
  GFSDK_Aftermath_CrashDump_Status status = GFSDK_Aftermath_CrashDump_Status_Unknown;
  while (GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GetCrashDumpStatus(&status)) &&
         status != GFSDK_Aftermath_CrashDump_Status_Finished &&
         status != GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(kDumpPoll);
  }
}

#else

void EnableGpuCrashDumps() {}

void AttachGpuCrashDumps(ID3D12Device& /*device*/) {}

void AwaitGpuCrashDump() {}

#endif

}  // namespace augusta::renderer
