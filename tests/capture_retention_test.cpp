#include "augusta/capture_retention.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/capture.h"
#include "augusta/capture_records.h"
#include "augusta/capturer.h"
#include "augusta/command.h"
#include "augusta/faults.h"
#include "augusta/logging.h"
#include "augusta/match.h"

// A capture directory kept within a count and a size, oldest Match first
// (ADR-0050, #461): PlanRetention's decision on its own, then a Capturer
// keeping a real directory, with completed captures seeded by hand.
namespace {

using augusta::capture::Capture;
using augusta::capture::kExtension;
using augusta::capture::ReadCapture;
using augusta::command::Command;
using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::host::CaptureEntrant;
using augusta::host::CaptureFileName;
using augusta::host::CaptureOptions;
using augusta::host::Capturer;
using augusta::host::CaptureRetention;
using augusta::host::CompletedCapture;
using augusta::host::EntityId;
using augusta::host::IsCaptureFileName;
using augusta::host::PlanRetention;
using augusta::host::RetentionPlan;
using augusta::host::SessionId;

using Milliseconds = std::chrono::sys_time<std::chrono::milliseconds>;

// 2026-10-09 10:15:00.123 UTC.
const Milliseconds kStarted{std::chrono::milliseconds{1'791'540'900'123}};
constexpr std::uint64_t kFirstTick = 1000;

// path in UTF-8: unlike path::string() on Windows, it never throws for a
// character the ANSI code page lacks.
std::string Utf8(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return {utf8.begin(), utf8.end()};
}

// A name no ANSI code page holds, so that path::string() throws for it on Windows.
const std::filesystem::path kOutsideTheCodePage(u8"中文");

std::vector<CompletedCapture> Sizes(const std::vector<std::uintmax_t>& sizes) {
  std::vector<CompletedCapture> completed;
  for (const std::uintmax_t size : sizes) {
    completed.push_back({.path = {}, .size = size});
  }
  return completed;
}

TEST(CaptureFileNameTest, NamesTheMatchsStartInUtcThenItsNumber) {
  EXPECT_EQ(CaptureFileName(kStarted, 3), "20261009T101500123Z-0003" + std::string(kExtension));
}

TEST(CaptureFileNameTest, OneServersNamesSortByWhenTheirMatchStarted) {
  const std::string first = CaptureFileName(kStarted, 9);
  const std::string second = CaptureFileName(kStarted + std::chrono::milliseconds{1}, 10);
  EXPECT_LT(first, second);
}

TEST(IsCaptureFileNameTest, AcceptsTheNamesCapturesAreGiven) {
  EXPECT_TRUE(IsCaptureFileName(CaptureFileName(kStarted, 1)));
  EXPECT_TRUE(IsCaptureFileName(CaptureFileName(kStarted, 12345)));
}

TEST(IsCaptureFileNameTest, RefusesAnyOtherName) {
  const std::string extension(kExtension);
  for (const std::string& name :
       {std::string("notes.txt"), "20261009T101500123Z-003" + extension,
        "20261009T101500123Z-0003" + extension + ".tmp", "20261009T101500123-0003" + extension,
        "2026100XT101500123Z-0003" + extension, std::string("20261009T101500123Z-0003.ACAP"),
        ".20261009T101500123Z-0003" + extension, std::string("20261009T101500123Z-0003"),
        std::string("20261009T101500123Z-0003.capture"), std::string()}) {
    EXPECT_FALSE(IsCaptureFileName(name)) << name;
  }
}

TEST(PlanRetentionTest, WithoutLimitsDeletesNothing) {
  EXPECT_EQ(PlanRetention(Sizes({100, 100}), 200, true, 1000, CaptureRetention{}),
            (RetentionPlan{.deletions = 0, .fits = true}));
}

TEST(PlanRetentionTest, OpeningLeavesFewerThanMaxFilesCompleted) {
  const CaptureRetention retention{.max_files = 3, .max_bytes = std::nullopt};

  EXPECT_EQ(PlanRetention(Sizes({1, 1, 1, 1, 1}), 5, true, 1, retention),
            (RetentionPlan{.deletions = 3, .fits = true}));
  EXPECT_EQ(PlanRetention(Sizes({1, 1}), 2, true, 1, retention), (RetentionPlan{.deletions = 0, .fits = true}));
}

TEST(PlanRetentionTest, WritingToTheCaptureOpenNeverCountsAFile) {
  const CaptureRetention retention{.max_files = 1, .max_bytes = std::nullopt};

  EXPECT_EQ(PlanRetention(Sizes({1, 1}), 3, false, 1, retention), (RetentionPlan{.deletions = 0, .fits = true}));
}

TEST(PlanRetentionTest, DeletesTheOldestUntilTheBytesFit) {
  const CaptureRetention retention{.max_files = std::nullopt, .max_bytes = 100};

  // 90 + 20 is over; without the oldest, 60 + 20 is not.
  EXPECT_EQ(PlanRetention(Sizes({30, 30, 30}), 90, false, 20, retention),
            (RetentionPlan{.deletions = 1, .fits = true}));
  // Exactly at the limit fits.
  EXPECT_EQ(PlanRetention(Sizes({30, 30, 30}), 90, false, 10, retention),
            (RetentionPlan{.deletions = 0, .fits = true}));
  EXPECT_EQ(PlanRetention(Sizes({10, 50, 30}), 90, false, 50, retention),
            (RetentionPlan{.deletions = 2, .fits = true}));
}

TEST(PlanRetentionTest, ACaptureOpenThatAloneFillsTheBudgetDoesNotFit) {
  const CaptureRetention retention{.max_files = std::nullopt, .max_bytes = 100};

  // Of the 95 bytes, 30 are a completed capture's and 65 the capture open's.
  EXPECT_EQ(PlanRetention(Sizes({30}), 95, false, 40, retention), (RetentionPlan{.deletions = 1, .fits = false}));
  EXPECT_EQ(PlanRetention(Sizes({}), 95, false, 10, retention), (RetentionPlan{.deletions = 0, .fits = false}));
}

TEST(PlanRetentionTest, DeletesWhatTheStricterLimitAsks) {
  const CaptureRetention retention{.max_files = 4, .max_bytes = 100};

  EXPECT_EQ(PlanRetention(Sizes({10, 10, 10, 10}), 40, true, 10, retention),
            (RetentionPlan{.deletions = 1, .fits = true}));
  EXPECT_EQ(PlanRetention(Sizes({50, 10, 10, 10}), 80, true, 30, retention),
            (RetentionPlan{.deletions = 1, .fits = true}));
  EXPECT_EQ(PlanRetention(Sizes({10, 10, 10, 60}), 90, true, 30, retention),
            (RetentionPlan{.deletions = 2, .fits = true}));
}

augusta::capture::Header Header() {
  augusta::capture::Header header{.engine_version = "2.0.1", .tick_rate_hz = 60, .started = {}};
  header.server_pack.fill(std::byte{1});
  header.client_pack.fill(std::byte{2});
  return header;
}

std::vector<CaptureEntrant> TwoPlayers() {
  return {CaptureEntrant{.session = SessionId{7}, .entity = EntityId{70}, .character = "soldier", .spawn = {1, 0, 2}},
          CaptureEntrant{.session = SessionId{8}, .entity = EntityId{80}, .character = "sniper", .spawn = {-3, 0, 4}}};
}

Command Walk() {
  Command command;
  command.movement.direction = {1.0F, 0.0F, 0.0F};
  command.fire = true;
  return command;
}

std::size_t Occurrences(std::string_view text, std::string_view part) {
  std::size_t count = 0;
  for (std::size_t at = text.find(part); at != std::string_view::npos; at = text.find(part, at + part.size())) {
    ++count;
  }
  return count;
}

class CaptureRetentionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Named after the running test: ctest runs every test in a process of its
    // own, possibly in parallel.
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    std::string name = std::string("augusta_capture_retention_test_") + test.test_suite_name() + "_" + test.name();
    std::ranges::replace(name, '/', '_');
    directory_ = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(directory_);
    std::filesystem::create_directories(directory_);
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  // A completed capture of a Match that started hours_ago, size bytes long.
  std::string Seed(int hours_ago, std::uintmax_t size) {
    const std::string name = CaptureFileName(kStarted - std::chrono::hours{hours_ago}, 1);
    Write(directory_ / name, true, size);
    return name;
  }

  // A file of size bytes: a capture of no Match cut or padded with filler to
  // size when as_capture, so it starts with a capture's magic, else filler.
  static void Write(const std::filesystem::path& path, bool as_capture, std::uintmax_t size) {
    std::string bytes;
    if (as_capture) {
      std::ostringstream written(std::ios::binary);
      EXPECT_TRUE(
          augusta::capture::WriteCapture(written, Capture{.header = Header(), .joins = {}, .events = {}, .torn = false})
              .has_value());
      bytes = written.str();
    }
    bytes.resize(size, 'x');
    std::ofstream(path, std::ios::binary) << bytes;
  }

  // The names of the directory's regular files, sorted.
  [[nodiscard]] std::vector<std::string> Names() const {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
      if (entry.is_regular_file()) {
        names.push_back(Utf8(entry.path().filename()));
      }
    }
    std::ranges::sort(names);
    return names;
  }

  [[nodiscard]] std::uintmax_t Bytes() const {
    std::uintmax_t bytes = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
      bytes += entry.is_regular_file() ? entry.file_size() : 0;
    }
    return bytes;
  }

  [[nodiscard]] Capture Read(const std::string& name) const {
    std::ifstream in(directory_ / name, std::ios::binary);
    auto capture = ReadCapture(in);
    EXPECT_TRUE(capture.has_value()) << name;
    return capture.value_or(Capture{});
  }

  // How many records the capture name holds after its header: its Joins, then its events.
  [[nodiscard]] std::size_t Records(const std::string& name) const {
    const Capture capture = Read(name);
    return capture.joins.size() + capture.events.size();
  }

  // Plays one Match of commands Commands, started at started.
  static void Play(Capturer& capturer, Milliseconds started, int commands) {
    capturer.StartMatch(TwoPlayers(), kFirstTick, started);
    for (int command = 0; command < commands; ++command) {
      capturer.Command(kFirstTick + command, EntityId{70}, Walk());
    }
    capturer.EndMatch(kFirstTick + commands, std::nullopt);
  }

  std::filesystem::path directory_;
};

// Requirements: US-21
TEST_F(CaptureRetentionTest, AMatchStartLeavesFewerThanMaxFilesOlderCapturesDeletingTheOldestFirst) {
  std::vector<std::string> seeded;
  for (const int hours_ago : {5, 4, 3, 2, 1}) {
    seeded.push_back(Seed(hours_ago, 100));
  }
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.retention = {.max_files = 3, .max_bytes = std::nullopt}});
    Play(capturer, kStarted, 1);
  }

  EXPECT_EQ(Names(), (std::vector<std::string>{seeded[3], seeded[4], CaptureFileName(kStarted, 1)}));
}

// Requirements: US-21
TEST_F(CaptureRetentionTest, WithoutRetentionNothingIsDeleted) {
  const std::string seeded = Seed(1, 100);
  {
    Capturer capturer(directory_, Header());
    Play(capturer, kStarted, 1);
  }

  EXPECT_EQ(Names(), (std::vector<std::string>{seeded, CaptureFileName(kStarted, 1)}));
}

// Requirements: US-21
TEST_F(CaptureRetentionTest, ADirectoryNearItsBudgetHasItsOldestCapturesDeletedWhileTheMatchIsWritten) {
  std::vector<std::string> seeded;
  for (const int hours_ago : {4, 3, 2, 1}) {
    seeded.push_back(Seed(hours_ago, 200));
  }
  constexpr std::uintmax_t kBudget = 1000;
  {
    Capturer capturer(directory_, Header(),
                      CaptureOptions{.retention = {.max_files = std::nullopt, .max_bytes = kBudget}});
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    for (int command = 0; command < 20; ++command) {
      capturer.Command(kFirstTick + command, EntityId{70}, Walk());
      capturer.WaitUntilWritten();
      ASSERT_LE(Bytes(), kBudget);
    }
    capturer.EndMatch(kFirstTick + 20, std::nullopt);
  }

  const std::vector<std::string> names = Names();
  ASSERT_GE(names.size(), 2U);
  ASSERT_LT(names.size(), seeded.size() + 1);
  // What remains of the seeded is the newest of them, and the Match whole.
  const std::vector<std::string> kept(names.begin(), names.end() - 1);
  EXPECT_EQ(kept, std::vector<std::string>(seeded.end() - static_cast<std::ptrdiff_t>(kept.size()), seeded.end()));
  EXPECT_EQ(names.back(), CaptureFileName(kStarted, 1));
  EXPECT_EQ(Records(names.back()), 2U + 20U + 1U);
  EXPECT_LE(Bytes(), kBudget);
}

// A Match larger than the budget on its own: the Match in progress is never
// deleted, so the capture stops and keeps every record up to there.
// Requirements: US-21
TEST_F(CaptureRetentionTest, AMatchThatAloneExceedsTheBudgetStopsItsCaptureWithRetentionBudget) {
  const std::string seeded = Seed(1, 100);
  constexpr std::uintmax_t kBudget = 400;
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  testing::internal::CaptureStdout();
  {
    Capturer capturer(directory_, Header(),
                      CaptureOptions{.retention = {.max_files = std::nullopt, .max_bytes = kBudget}});
    Play(capturer, kStarted, 100);
  }
  const std::string written = testing::internal::GetCapturedStdout();

  EXPECT_EQ(Names(), std::vector<std::string>{CaptureFileName(kStarted, 1)});
  EXPECT_LE(Bytes(), kBudget);
  const Capture capture = Read(CaptureFileName(kStarted, 1));
  EXPECT_FALSE(capture.torn);
  EXPECT_EQ(capture.joins.size(), 2U);
  EXPECT_FALSE(capture.events.empty());
  EXPECT_LT(capture.events.size(), 100U + 1U);
  EXPECT_EQ(Occurrences(written, "event=capture_stopped match=1 reason=retention_budget"), 1U) << written;
}

// Requirements: US-21
TEST_F(CaptureRetentionTest, TheMatchInProgressIsNeverDeleted) {
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.retention = {.max_files = 1, .max_bytes = std::nullopt}});
    Play(capturer, kStarted, 3);
    Play(capturer, kStarted + std::chrono::seconds{1}, 3);
  }

  const std::string second = CaptureFileName(kStarted + std::chrono::seconds{1}, 2);
  EXPECT_EQ(Names(), std::vector<std::string>{second});
  EXPECT_EQ(Records(second), 2U + 3U + 1U);
}

// Requirements: US-21
TEST_F(CaptureRetentionTest, FilesThatAreNotCapturesAndOtherDirectoriesAreNeverTouched) {
  Write(directory_ / "notes.txt", true, 100);
  const std::string unmarked = CaptureFileName(kStarted - std::chrono::hours{3}, 1);
  Write(directory_ / unmarked, false, 100);
  const std::filesystem::path nested = directory_ / CaptureFileName(kStarted - std::chrono::hours{2}, 1);
  std::filesystem::create_directory(nested);
  Write(nested / CaptureFileName(kStarted - std::chrono::hours{2}, 1), true, 100);
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.retention = {.max_files = 1, .max_bytes = 10'000}});
    Play(capturer, kStarted, 0);
  }

  EXPECT_EQ(Names(), (std::vector<std::string>{unmarked, CaptureFileName(kStarted, 1), "notes.txt"}));
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(nested), {}), 1);
}

// A file whose name the ANSI code page lacks is a file like any other: being no
// capture's name, it is never touched, and the Match is captured.
// Requirements: US-21
TEST_F(CaptureRetentionTest, AFileNamedOutsideTheCodePageIsLeftAloneAndTheMatchIsCaptured) {
  const std::string seeded = Seed(1, 100);
  std::filesystem::path outside = kOutsideTheCodePage;
  outside += kExtension;
  Write(directory_ / outside, true, 100);
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.retention = {.max_files = 1, .max_bytes = std::nullopt}});
    Play(capturer, kStarted, 1);
  }

  EXPECT_EQ(Names(), (std::vector<std::string>{CaptureFileName(kStarted, 1), Utf8(outside)}));
  EXPECT_EQ(Records(CaptureFileName(kStarted, 1)), 2U + 1U + 1U);
}

// The directory's own path, which every line naming a capture logs as it
// starts, ends or is deleted, holds what the ANSI code page lacks: the
// capture is written whole, and the lines name it in UTF-8.
// Requirements: US-21
TEST_F(CaptureRetentionTest, ACaptureDirectoryNamedOutsideTheCodePageHoldsWholeCaptures) {
  const std::filesystem::path directory = directory_ / kOutsideTheCodePage;
  std::filesystem::create_directories(directory);
  Write(directory / CaptureFileName(kStarted - std::chrono::hours{1}, 1), true, 100);
  const std::filesystem::path captured = directory / CaptureFileName(kStarted, 1);
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  testing::internal::CaptureStdout();
  {
    Capturer capturer(directory, Header(), CaptureOptions{.retention = {.max_files = 1, .max_bytes = std::nullopt}});
    Play(capturer, kStarted, 3);
  }
  const std::string written = testing::internal::GetCapturedStdout();

  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), {}), 1);
  std::ifstream in(captured, std::ios::binary);
  const auto capture = ReadCapture(in);
  ASSERT_TRUE(capture.has_value());
  EXPECT_EQ(capture->joins.size() + capture->events.size(), 2U + 3U + 1U);
  EXPECT_NE(written.find(Utf8(captured)), std::string::npos) << written;
  EXPECT_EQ(Occurrences(written, "event=capture_deleted"), 1U) << written;
}

// Requirements: US-21
TEST_F(CaptureRetentionTest, AFailedDeletionIsLoggedOnceAndTheMatchIsStillCaptured) {
  std::vector<std::string> seeded;
  for (const int hours_ago : {3, 2, 1}) {
    seeded.push_back(Seed(hours_ago, 100));
  }
  Faults faults;
  faults.Arm(Site::kCaptureDelete, "permission denied", Faults::kEveryTime);
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  testing::internal::CaptureStdout();
  {
    Capturer capturer(directory_, Header(),
                      CaptureOptions{.faults = &faults, .retention = {.max_files = 1, .max_bytes = std::nullopt}});
    Play(capturer, kStarted, 2);
  }
  const std::string written = testing::internal::GetCapturedStdout();

  EXPECT_EQ(Occurrences(written, "event=capture_retention_failed"), 1U) << written;
  EXPECT_EQ(Occurrences(written, "event=capture_stopped"), 0U) << written;
  EXPECT_EQ(Names().size(), seeded.size() + 1);
  EXPECT_EQ(Records(CaptureFileName(kStarted, 1)), 2U + 2U + 1U);
}

// NFR-01: what retention deletes, it deletes on the capture's writer thread,
// never on the thread handing the Capturer its events.
// Requirements: NFR-01, US-21
TEST_F(CaptureRetentionTest, DeletionsRunOffTheCallersThreadAndAreObserved) {
  for (const int hours_ago : {3, 2, 1}) {
    Seed(hours_ago, 100);
  }
  // What the Capturer tells its observer of retention, and on which thread.
  struct Told final : augusta::host::CaptureObserver {
    std::vector<std::thread::id> deleters;
    augusta::host::CaptureDirectoryUsage usage;
    void OnRetentionDeleted() override { deleters.push_back(std::this_thread::get_id()); }
    void OnDirectory(augusta::host::CaptureDirectoryUsage directory) override { usage = directory; }
  } told;
  {
    Capturer capturer(directory_, Header(),
                      CaptureOptions{.retention = {.max_files = 2, .max_bytes = std::nullopt}, .observer = &told});
    Play(capturer, kStarted, 2);
  }

  ASSERT_EQ(told.deleters.size(), 2U);
  EXPECT_TRUE(std::ranges::none_of(told.deleters, [](std::thread::id id) { return id == std::this_thread::get_id(); }));
  EXPECT_EQ(told.usage.files, 2U);
  EXPECT_EQ(told.usage.bytes, Bytes());
}

}  // namespace
