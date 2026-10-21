#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "augusta/gpu_crash_dump_files.h"

// Where a GPU crash's Aftermath dumps land (ADR-0055): under the naming the
// client gives, numbered from 1 in the order they come.
namespace {

using augusta::renderer::GpuCrashDumpFiles;

class GpuCrashDumpFilesTest : public testing::Test {
 protected:
  void SetUp() override {
    const testing::TestInfo* info = testing::UnitTest::GetInstance()->current_test_info();
    directory_ = std::filesystem::temp_directory_path() / (std::string("augusta-gpu-crash-dumps-") + info->name());
    std::filesystem::remove_all(directory_);
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& Directory() const { return directory_; }

  static std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }

 private:
  std::filesystem::path directory_;
};

constexpr std::array kDump{std::byte{'g'}, std::byte{'p'}, std::byte{'u'}};

TEST_F(GpuCrashDumpFilesTest, EachDumpIsWrittenWhereTheNamingPutsItsNumberFromOne) {
  GpuCrashDumpFiles files(
      [this](std::uint32_t n) { return Directory() / "dumps" / std::format("run-{}.nv-gpudmp", n); });

  const auto first = files.Write(kDump);
  const auto second = files.Write(kDump);

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, Directory() / "dumps" / "run-1.nv-gpudmp");
  EXPECT_EQ(*second, Directory() / "dumps" / "run-2.nv-gpudmp");
  EXPECT_EQ(Read(*first), "gpu");
  EXPECT_EQ(Read(*second), "gpu");
}

TEST_F(GpuCrashDumpFilesTest, ADumpThatCannotBeWrittenIsReportedWithItsPath) {
  // A file where its folder should be.
  std::filesystem::create_directories(Directory());
  std::ofstream(Directory() / "dumps") << "not a folder";
  GpuCrashDumpFiles files([this](std::uint32_t /*n*/) { return Directory() / "dumps" / "run.nv-gpudmp"; });

  const auto written = files.Write(kDump);

  ASSERT_FALSE(written.has_value());
  EXPECT_EQ(written.error(), Directory() / "dumps" / "run.nv-gpudmp");
}

}  // namespace
