#include <cstdint>
#include <filesystem>
#include <format>

#include <gtest/gtest.h>

#include "augusta/gpu_crash_dump_files.h"

// The directory a GPU crash dump would land in, which the Profiling Debug panel
// shows: in a Development build only (ADR-0074).
namespace {

using augusta::renderer::GpuCrashDumpFiles;

// Requirements: NFR-10
TEST(GpuCrashDumpFilesDirectoryTest, IsTheOneTheNextDumpLandsIn) {
  const std::filesystem::path dumps = std::filesystem::temp_directory_path() / "dumps";
  const GpuCrashDumpFiles files([&dumps](std::uint32_t n) { return dumps / std::format("run-{}.nv-gpudmp", n); });

  EXPECT_EQ(files.Directory(), dumps);
}

// Requirements: NFR-10
TEST(GpuCrashDumpFilesDirectoryTest, IsTheWorkingDirectoryForANamingWithoutAFolder) {
  const GpuCrashDumpFiles files([](std::uint32_t n) { return std::filesystem::path(std::format("dump-{}", n)); });

  EXPECT_EQ(files.Directory(), std::filesystem::current_path());
}

// Requirements: NFR-10
TEST(GpuCrashDumpFilesDirectoryTest, MakesARelativeFolderAbsolute) {
  const GpuCrashDumpFiles files(
      [](std::uint32_t n) { return std::filesystem::path("dumps") / std::format("dump-{}", n); });

  EXPECT_EQ(files.Directory(), std::filesystem::current_path() / "dumps");
}

}  // namespace
