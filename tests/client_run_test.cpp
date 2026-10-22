#include "augusta/client_run.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

#include "augusta/config.h"
#include "augusta/gpu_crash_dump_files.h"
#include "augusta/logging.h"
#include "augusta/run_files.h"
#include "augusta/uuid.h"

// What a launch tells the player of the Run before it (ADR-0027): a crashed
// one, in one WARN line naming it and its files, and nothing otherwise. And
// the client's Session in its log (ADR-0029): from its admission to its end,
// every line carries `session=`, and none does after.
namespace {

using augusta::client::GpuDumpPath;
using augusta::client::LogPath;
using augusta::client::MinidumpPath;
using augusta::client::ReportPreviousRun;
using augusta::client::SessionLogContext;
using augusta::uuid::Uuid;

Uuid RunId(std::uint8_t n) {
  return Uuid::FromBytes({0x01, 0x9a, 0x07, 0x5c, 0x4f, 0x10, 0x7a, 0x01, 0x8b, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, n});
}

std::size_t Occurrences(std::string_view text, std::string_view needle) {
  std::size_t count = 0;
  for (std::size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

class ClientRunTest : public testing::Test {
 protected:
  void SetUp() override {
    const testing::TestInfo* info = testing::UnitTest::GetInstance()->current_test_info();
    directory_ = std::filesystem::temp_directory_path() / (std::string("augusta-client-run-") + info->name());
    std::filesystem::remove_all(directory_);
    augusta::logging::Init();
    augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& Directory() const { return directory_; }

  static void Write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
  }

  // What ReportPreviousRun writes for the Run current.
  [[nodiscard]] std::string Reported(const Uuid& current) const {
    testing::internal::CaptureStdout();
    ReportPreviousRun(directory_, current);
    return testing::internal::GetCapturedStdout();
  }

 private:
  std::filesystem::path directory_;
};

TEST_F(ClientRunTest, ALaunchAfterACrashedRunWritesOneWarningNamingItAndItsFiles) {
  Write(LogPath(Directory(), RunId(1)), "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crash signal=SIGSEGV\n");
  Write(LogPath(Directory(), RunId(2)), "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crash signal=SIGSEGV\n");
  Write(MinidumpPath(Directory(), RunId(2)), "dump");

  const std::string written = Reported(RunId(3));

  EXPECT_EQ(Occurrences(written, "\n"), 1U) << written;
  const std::u8string directory = Directory().u8string();
  const std::string id = RunId(2).ToString();
  EXPECT_EQ(Occurrences(written, "WARN subsystem=client event=previous_run_crashed crashed_run=" + id +
                                     " directory=\"" + std::string(directory.begin(), directory.end()) +
                                     "\" files=\"dumps/augustac-" + id + ".dmp logs/augustac-" + id + ".log\""),
            1U)
      << written;
}

TEST_F(ClientRunTest, ALaunchAfterAnyOtherEndWritesNothing) {
  Write(LogPath(Directory(), RunId(1)), "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crash signal=SIGSEGV\n");
  Write(LogPath(Directory(), RunId(2)),
        "2026-10-21T10:00:01Z CRITICAL subsystem=client event=terminal_failure code=server_unreachable\n");

  EXPECT_EQ(Reported(RunId(3)), "");
  EXPECT_EQ(Reported(RunId(1)), "");
}

// BeginRun's files are beside the test's executable, as augustac's are beside
// it; the dump it names is removed after.
TEST_F(ClientRunTest, ARunNamesItsGpuCrashDumpsAfterItsId) {
  const augusta::client::Run run = augusta::client::BeginRun();

  const auto written = augusta::renderer::WriteGpuCrashDump(std::as_bytes(std::span("gpu")));

  ASSERT_TRUE(written.has_value());
  const auto executable_directory = augusta::config::ExecutableDirectory();
  ASSERT_TRUE(executable_directory.has_value());
  EXPECT_EQ(*written, GpuDumpPath(*executable_directory, run.id, 1));
  std::error_code ignored;
  std::filesystem::remove(*written, ignored);
}

const Uuid kSession =
    Uuid::FromBytes({0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD4, 0x70, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11});

// What one LOG_INFO of message writes to the console.
std::string WrittenInfoLine(std::string_view message) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  testing::internal::CaptureStdout();
  LOG_INFO("{}", message);
  return testing::internal::GetCapturedStdout();
}

TEST(SessionLogContextTest, EveryLineWhileTheSessionIsHeldCarriesItsSessionId) {
  const SessionLogContext held(kSession);

  const std::string written = WrittenInfoLine("subsystem=test event=admitted");

  EXPECT_NE(written.find("event=admitted session=0190a1b2-c3d4-7000-8000-000000000011\n"), std::string::npos)
      << written;
}

TEST(SessionLogContextTest, NoLineAfterTheSessionEndsCarriesASessionId) {
  {
    const SessionLogContext held(kSession);
  }

  const std::string written = WrittenInfoLine("subsystem=test event=ended");

  EXPECT_EQ(written.find("session="), std::string::npos) << written;
}

}  // namespace
