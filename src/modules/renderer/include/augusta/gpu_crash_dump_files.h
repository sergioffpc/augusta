#ifndef AUGUSTA_GPU_CRASH_DUMP_FILES_H_
#define AUGUSTA_GPU_CRASH_DUMP_FILES_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <span>

/// \file
/// The files the Renderer's GPU crash dumps are written to (ADR-0055): a dump
/// is mechanism here, its name the caller's decision. Aftermath watches the
/// whole process, so the files its dumps go to are the process's too
/// (NameGpuCrashDumps); the client names them after its Run (client_run.h).
/// Free of any GPU API, so it is built and tested on every platform.
namespace augusta::renderer {

/// The path of the process's n-th GPU crash dump, from 1.
using GpuCrashDumpNaming = std::function<std::filesystem::path(std::uint32_t n)>;

/// Writes dumps where a naming puts them, numbered in the order they come: a
/// fault can be reported more than once.
class GpuCrashDumpFiles {
 public:
  explicit GpuCrashDumpFiles(GpuCrashDumpNaming naming);

  /// Writes dump to the next number's path, creating its folder, and returns
  /// that path, or it as the error if the dump could not be written there.
  /// From any thread.
  std::expected<std::filesystem::path, std::filesystem::path> Write(std::span<const std::byte> dump);

 private:
  std::mutex mutex_;
  GpuCrashDumpNaming naming_;
  std::uint32_t count_ = 0;
};

/// Names the process's GPU crash dumps from then on, numbered from 1 again.
/// Until it is called they are `gpu-crash-<n>.nv-gpudmp` in the working
/// directory. Call it before the Renderer is made, so no dump comes first.
void NameGpuCrashDumps(GpuCrashDumpNaming naming);

/// Writes the process's next GPU crash dump as NameGpuCrashDumps names it: what
/// the Renderer's Aftermath callback calls (vulkan_gpu_crash_dumps.h).
std::expected<std::filesystem::path, std::filesystem::path> WriteGpuCrashDump(std::span<const std::byte> dump);

}  // namespace augusta::renderer

#endif  // AUGUSTA_GPU_CRASH_DUMP_FILES_H_
