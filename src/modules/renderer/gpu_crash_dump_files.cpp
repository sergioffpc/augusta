#include "augusta/gpu_crash_dump_files.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <mutex>
#include <span>
#include <system_error>
#include <utility>

namespace augusta::renderer {

namespace {

// The process's GPU crash dump files, as NameGpuCrashDumps last named them,
// under GlobalFilesMutex. Never destroyed, so a dump during exit still finds
// them; a dump being written keeps the files it started with.
std::shared_ptr<GpuCrashDumpFiles>& GlobalFiles() {
  static auto* files = new std::shared_ptr<GpuCrashDumpFiles>(std::make_shared<GpuCrashDumpFiles>(
      [](std::uint32_t n) { return std::filesystem::path(std::format("gpu-crash-{}.nv-gpudmp", n)); }));
  return *files;
}

std::mutex& GlobalFilesMutex() {
  static auto* mutex = new std::mutex;
  return *mutex;
}

}  // namespace

GpuCrashDumpFiles::GpuCrashDumpFiles(GpuCrashDumpNaming naming) : naming_(std::move(naming)) {}

std::expected<std::filesystem::path, std::filesystem::path> GpuCrashDumpFiles::Write(std::span<const std::byte> dump) {
  std::filesystem::path path;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    path = naming_(++count_);
  }
  if (path.has_parent_path()) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      return std::unexpected(std::move(path));
    }
  }
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(dump.data()), static_cast<std::streamsize>(dump.size()));
  file.close();
  if (!file) {
    return std::unexpected(std::move(path));
  }
  return path;
}

void NameGpuCrashDumps(GpuCrashDumpNaming naming) {
  auto files = std::make_shared<GpuCrashDumpFiles>(std::move(naming));
  const std::lock_guard<std::mutex> lock(GlobalFilesMutex());
  GlobalFiles().swap(files);
}

std::expected<std::filesystem::path, std::filesystem::path> WriteGpuCrashDump(std::span<const std::byte> dump) {
  std::shared_ptr<GpuCrashDumpFiles> files;
  {
    const std::lock_guard<std::mutex> lock(GlobalFilesMutex());
    files = GlobalFiles();
  }
  return files->Write(dump);
}

}  // namespace augusta::renderer
