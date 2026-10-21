#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/run_files.h"
#include "augusta/uuid.h"

// The files a Run of augustac leaves beside its executable (GLOSSARY.md, ADR-0027,
// ADR-0055): their names from the Run ID, the last Runs kept, and how the
// previous Run ended, each over a temporary directory.
namespace {

using augusta::client::FindPreviousRun;
using augusta::client::GpuDumpPath;
using augusta::client::kKeptRuns;
using augusta::client::LogPath;
using augusta::client::MinidumpPath;
using augusta::client::PruneRuns;
using augusta::client::ReadRunEnd;
using augusta::client::RunEnd;
using augusta::client::RunFiles;
using augusta::uuid::Uuid;

// The Run ID whose last byte is n and whose others are a fixed UUIDv7's: a
// larger n is a later Run.
Uuid RunId(std::uint8_t n) {
  return Uuid::FromBytes({0x01, 0x9a, 0x07, 0x5c, 0x4f, 0x10, 0x7a, 0x01, 0x8b, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, n});
}

// A directory of its own under the system's temporary one, removed with it.
class RunFilesTest : public testing::Test {
 protected:
  void SetUp() override {
    const testing::TestInfo* info = testing::UnitTest::GetInstance()->current_test_info();
    directory_ = std::filesystem::temp_directory_path() /
                 (std::string("augusta-run-files-") + info->test_suite_name() + "-" + info->name());
    std::filesystem::remove_all(directory_);
    std::filesystem::create_directories(directory_);
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& Directory() const { return directory_; }

  // Writes text to path, its directory created if missing.
  static void Write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
  }

  // Leaves a log, a minidump and one GPU crash dump for run.
  void LeaveRun(const Uuid& run) const {
    Write(LogPath(directory_, run), "line\n");
    Write(MinidumpPath(directory_, run), "dump");
    Write(GpuDumpPath(directory_, run, 1), "gpu dump");
  }

 private:
  std::filesystem::path directory_;
};

TEST_F(RunFilesTest, ARunsFilesAreNamedByItsRunId) {
  const Uuid run = RunId(7);
  const std::string id = "019a075c-4f10-7a01-8b02-000000000007";

  EXPECT_EQ(LogPath(Directory(), run), Directory() / "logs" / ("augustac-" + id + ".log"));
  EXPECT_EQ(MinidumpPath(Directory(), run), Directory() / "dumps" / ("augustac-" + id + ".dmp"));
  EXPECT_EQ(GpuDumpPath(Directory(), run, 2), Directory() / "dumps" / ("augustac-" + id + "-2.nv-gpudmp"));
}

TEST_F(RunFilesTest, ARunsFilesAreThoseNamedByItsIdInBothFolders) {
  LeaveRun(RunId(1));
  LeaveRun(RunId(2));
  Write(GpuDumpPath(Directory(), RunId(1), 2), "second gpu dump");

  const std::vector<std::filesystem::path> expected{
      MinidumpPath(Directory(), RunId(1)), GpuDumpPath(Directory(), RunId(1), 1), GpuDumpPath(Directory(), RunId(1), 2),
      LogPath(Directory(), RunId(1))};
  std::vector<std::filesystem::path> files = RunFiles(Directory(), RunId(1));
  std::ranges::sort(files);
  std::vector<std::filesystem::path> sorted = expected;
  std::ranges::sort(sorted);
  EXPECT_EQ(files, sorted);
}

TEST_F(RunFilesTest, PruningKeepsTheNewestRunsFilesInBothFoldersAndDeletesTheRest) {
  constexpr std::uint8_t kRuns = 13;
  for (std::uint8_t n = 1; n <= kRuns; ++n) {
    LeaveRun(RunId(n));
  }

  const auto undeleted = PruneRuns(Directory(), kKeptRuns);

  EXPECT_TRUE(undeleted.empty());
  for (std::uint8_t n = 1; n <= kRuns; ++n) {
    const bool kept = n > kRuns - kKeptRuns;
    EXPECT_EQ(std::filesystem::exists(LogPath(Directory(), RunId(n))), kept) << int{n};
    EXPECT_EQ(std::filesystem::exists(MinidumpPath(Directory(), RunId(n))), kept) << int{n};
    EXPECT_EQ(std::filesystem::exists(GpuDumpPath(Directory(), RunId(n), 1)), kept) << int{n};
  }
}

// A Run with only a dump left, or only a log, counts as one of the Runs kept
// all the same: the folders are pruned together, by Run ID.
TEST_F(RunFilesTest, PruningCountsARunOnceAcrossBothFolders) {
  for (std::uint8_t n = 1; n <= kKeptRuns; ++n) {
    Write(n % 2 == 0 ? LogPath(Directory(), RunId(n)) : MinidumpPath(Directory(), RunId(n)), "x");
  }
  Write(LogPath(Directory(), RunId(kKeptRuns)), "x");

  EXPECT_TRUE(PruneRuns(Directory(), kKeptRuns).empty());

  for (std::uint8_t n = 1; n <= kKeptRuns; ++n) {
    EXPECT_FALSE(RunFiles(Directory(), RunId(n)).empty()) << int{n};
  }
}

TEST_F(RunFilesTest, PruningLeavesAloneAFileThatIsNotARuns) {
  for (std::uint8_t n = 1; n <= kKeptRuns + 1; ++n) {
    LeaveRun(RunId(n));
  }
  const std::vector<std::filesystem::path> strangers{
      Directory() / "logs" / "notes.txt",
      Directory() / "logs" / "augustac-not-a-run-id.log",
      Directory() / "logs" / "augustac-019A075C-4F10-7A01-8B02-0000000000FF.log",
      Directory() / "logs" / ("augustac-" + RunId(1).ToString() + ".dmp"),
      Directory() / "dumps" / ("augustac-" + RunId(1).ToString() + ".log"),
      Directory() / "dumps" / ("augustac-" + RunId(1).ToString() + "-x.nv-gpudmp"),
      Directory() / "dumps" / ("augustad-" + RunId(1).ToString() + ".dmp"),
  };
  for (const auto& stranger : strangers) {
    Write(stranger, "keep me");
  }
  std::filesystem::create_directories(Directory() / "logs" / ("augustac-" + RunId(1).ToString() + ".log.d"));

  EXPECT_TRUE(PruneRuns(Directory(), kKeptRuns).empty());

  EXPECT_FALSE(std::filesystem::exists(LogPath(Directory(), RunId(1))));
  for (const auto& stranger : strangers) {
    EXPECT_TRUE(std::filesystem::exists(stranger)) << stranger;
  }
}

TEST_F(RunFilesTest, PruningADirectoryWithNoRunsDoesNothing) {
  EXPECT_TRUE(PruneRuns(Directory(), kKeptRuns).empty());
  EXPECT_TRUE(PruneRuns(Directory() / "missing", kKeptRuns).empty());
}

// A crash leaves the crash line, then a line per frame (ADR-0047).
TEST_F(RunFilesTest, ALogEndingInACrashLineIsACrashedRun) {
  const std::filesystem::path log = Directory() / "crashed.log";
  Write(log,
        "2026-10-21T10:00:00Z INFO subsystem=client event=starting version=1.0.0 run=x\n"
        "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crash signal=SIGSEGV\n"
        "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crash_frame index=0 pc=0x1234\n");

  EXPECT_EQ(ReadRunEnd(log), RunEnd::kCrash);

  Write(log, "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crash signal=SIGSEGV\n");
  EXPECT_EQ(ReadRunEnd(log), RunEnd::kCrash);
}

TEST_F(RunFilesTest, ALogEndingInTheTerminalEventOfAFailureIsATerminalFailure) {
  const std::filesystem::path log = Directory() / "failed.log";
  Write(log,
        "2026-10-21T10:00:00Z INFO subsystem=client event=starting version=1.0.0\r\n"
        "2026-10-21T10:00:01Z CRITICAL subsystem=client event=terminal_failure code=server_unreachable "
        "disposition=session detail=\"event=crash\" exit_status=1 run=x\r\n\r\n");

  EXPECT_EQ(ReadRunEnd(log), RunEnd::kTerminalFailure);
}

TEST_F(RunFilesTest, AnyOtherLastLineIsAnOrdinaryEnd) {
  const std::filesystem::path log = Directory() / "stopped.log";
  for (const std::string_view last : {
           "2026-10-21T10:00:01Z INFO subsystem=client event=exiting exit_status=0 run=x",
           "2026-10-21T10:00:01Z INFO subsystem=client event=frame detail=\"event=crash\"",
           "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crashed",
           "garbage",
       }) {
    Write(log, "2026-10-21T10:00:00Z CRITICAL subsystem=client event=crash signal=SIGSEGV\n" + std::string(last));
    EXPECT_EQ(ReadRunEnd(log), RunEnd::kOrdinary) << last;
  }
}

TEST_F(RunFilesTest, AnEmptyOrMissingLogIsAnOrdinaryEnd) {
  const std::filesystem::path log = Directory() / "empty.log";
  Write(log, "");

  EXPECT_EQ(ReadRunEnd(log), RunEnd::kOrdinary);
  EXPECT_EQ(ReadRunEnd(Directory() / "missing.log"), RunEnd::kOrdinary);
}

TEST_F(RunFilesTest, ThePreviousRunIsTheNewestBeforeThisOneWithHowItEnded) {
  LeaveRun(RunId(1));
  Write(LogPath(Directory(), RunId(2)), "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crash signal=SIGSEGV\n");
  Write(MinidumpPath(Directory(), RunId(2)), "dump");
  Write(LogPath(Directory(), RunId(3)), "2026-10-21T10:00:00Z INFO subsystem=client event=starting\n");

  const auto previous = FindPreviousRun(Directory(), RunId(3));

  ASSERT_TRUE(previous.has_value());
  EXPECT_EQ(previous->id, RunId(2));
  EXPECT_EQ(previous->end, RunEnd::kCrash);
  std::vector<std::filesystem::path> files = previous->files;
  std::ranges::sort(files);
  EXPECT_EQ(files, (std::vector{MinidumpPath(Directory(), RunId(2)), LogPath(Directory(), RunId(2))}));
}

TEST_F(RunFilesTest, AFirstRunHasNoPreviousRun) {
  Write(LogPath(Directory(), RunId(3)), "2026-10-21T10:00:00Z INFO subsystem=client event=starting\n");

  EXPECT_FALSE(FindPreviousRun(Directory(), RunId(3)).has_value());
  EXPECT_FALSE(FindPreviousRun(Directory() / "missing", RunId(3)).has_value());
}

// A crashed Run whose log is gone left only its dump: no line says it crashed.
TEST_F(RunFilesTest, APreviousRunWithNoLogEndedOrdinarily) {
  Write(MinidumpPath(Directory(), RunId(2)), "dump");

  const auto previous = FindPreviousRun(Directory(), RunId(3));

  ASSERT_TRUE(previous.has_value());
  EXPECT_EQ(previous->end, RunEnd::kOrdinary);
}

}  // namespace
