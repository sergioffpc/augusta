#include "vulkan_gpu_crash_dumps.h"

#if AUGUSTA_HAS_AFTERMATH

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <thread>

#include <GFSDK_Aftermath_Defines.h>
#include <GFSDK_Aftermath_GpuCrashDump.h>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/system/error_code.hpp>

#include "augusta/gpu_crash_dump_files.h"
#include "augusta/logging.h"
#include "augusta/version.h"

#endif

namespace augusta::renderer {

#if AUGUSTA_HAS_AFTERMATH

namespace {

// path in UTF-8: path::string() converts to the ANSI code page on Windows, and
// throws for a character it lacks.
std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return {text.begin(), text.end()};
}

// How long AwaitGpuCrashDump waits for Aftermath, and how often it checks.
constexpr std::chrono::seconds kDumpTimeout{5};
constexpr std::chrono::milliseconds kDumpPoll{50};

// Empty if the executable can't be found.
std::filesystem::path ExecutablePath() {
  boost::system::error_code error;
  const auto executable = boost::dll::program_location(error);
  if (error) {
    return {};
  }
  // native(): a std::wstring on Windows, so non-ASCII directories survive.
  return {executable.native()};
}

// Writes a dump where the process's GPU crash dumps are named
// (gpu_crash_dump_files.h): a fault can be reported more than once. Logged as a
// warning, not an error: the device lost is thrown, and the boundary that
// catches it writes the one ERR or CRIT line (ADR-0033).
void WriteDump(const void* dump, std::uint32_t size, void* /*user_data*/) {
  const auto written = WriteGpuCrashDump(std::span(static_cast<const std::byte*>(dump), size));
  if (!written) {
    LOG_WARN("subsystem=renderer event=gpu_crash_dump_failed path=\"{}\"", Utf8(written.error()));
    return;
  }
  LOG_WARN("subsystem=renderer event=gpu_crash_dump path=\"{}\"", Utf8(*written));
}

void DescribeDump(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription add, void* /*user_data*/) {
  add(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, Utf8(ExecutablePath().stem()).c_str());
  add(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationVersion, std::string(EngineVersion()).c_str());
}

bool StartAftermath() {
  const GFSDK_Aftermath_Result result = GFSDK_Aftermath_EnableGpuCrashDumps(
      GFSDK_Aftermath_Version_API, GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,
      GFSDK_Aftermath_GpuCrashDumpFeatureFlags_Default, WriteDump, nullptr, DescribeDump, nullptr, nullptr);
  if (!GFSDK_Aftermath_SUCCEED(result)) {
    LOG_WARN("subsystem=renderer event=gpu_crash_dumps_unavailable result={:#x}", static_cast<std::uint32_t>(result));
    return false;
  }
  LOG_INFO("subsystem=renderer event=gpu_crash_dumps_enabled");
  return true;
}

// Whether EnableGpuCrashDumps started Aftermath; read by AwaitGpuCrashDump.
std::atomic<bool> crash_dumps_enabled{false};

}  // namespace

bool EnableGpuCrashDumps() {
  static std::once_flag once;
  std::call_once(once, [] { crash_dumps_enabled = StartAftermath(); });
  return crash_dumps_enabled;
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

bool EnableGpuCrashDumps() { return false; }

void AwaitGpuCrashDump() {}

#endif

}  // namespace augusta::renderer
