#include "augusta/capturer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sstream>
#include <thread>

#include <sys/stat.h>
#endif

#include "augusta/capture.h"
#include "augusta/capture_records.h"
#include "augusta/capture_retention.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/match.h"
#include "augusta/math.h"

// A Match capture (ADR-0050), written by a Capturer into a directory of its
// own and read back by ReadCapture, with no Host: the events are handed in by
// hand, as Host hands them on the Simulation thread.
namespace {

using augusta::capture::ReadCapture;
using augusta::command::Command;
using augusta::failure::Code;
using augusta::failure::DescribeFailure;
using augusta::failure::Disposition;
using augusta::failure::DispositionOf;
using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::host::CaptureDirectoryUsage;
using augusta::host::CaptureEntrant;
using augusta::host::CaptureFileName;
using augusta::host::CaptureHealth;
using augusta::host::CaptureMode;
using augusta::host::CaptureObserver;
using augusta::host::CaptureOptions;
using augusta::host::Capturer;
using augusta::host::CaptureState;
using augusta::host::CaptureStep;
using augusta::host::CaptureStop;
using augusta::host::EntityId;
using augusta::host::LostCapture;
using augusta::host::SessionId;
using augusta::math::Vec3;

namespace capture = augusta::capture;

using Milliseconds = std::chrono::sys_time<std::chrono::milliseconds>;

// 2026-10-09 10:15:00.123 UTC.
const Milliseconds kStarted{std::chrono::milliseconds{1'791'540'900'123}};
constexpr std::uint64_t kFirstTick = 1000;

capture::Header Header() {
  capture::Header header{.engine_version = "2.0.1", .tick_rate_hz = 60, .started = {}};
  header.server_pack.fill(std::byte{1});
  header.client_pack.fill(std::byte{2});
  return header;
}

std::vector<CaptureEntrant> TwoPlayers() {
  return {CaptureEntrant{.session = SessionId{7}, .entity = EntityId{70}, .character = "soldier", .spawn = {1, 0, 2}},
          CaptureEntrant{.session = SessionId{8}, .entity = EntityId{80}, .character = "sniper", .spawn = {-3, 0, 4}}};
}

Command Walk(std::uint64_t seen_tick) {
  Command command;
  command.movement.direction = Vec3{1.0F, 0.0F, 0.0F};
  command.fire = true;
  command.seen_tick = seen_tick;
  return command;
}

// What a Capturer told its observer, as Told keeps it.
struct Telling {
  std::vector<CaptureState> states;
  std::uint64_t started = 0;
  std::uint64_t completed = 0;
  std::vector<CaptureStop> stops;
  std::uint64_t written = 0;
  // The last count of records queued, and the most.
  std::size_t queued = 0;
  std::size_t most_queued = 0;
  CaptureDirectoryUsage directory;
};

// Keeps what a Capturer tells it, from either of its threads.
class Told final : public CaptureObserver {
 public:
  void OnState(CaptureState state) override {
    Update([&](Telling& told) { told.states.push_back(state); });
  }
  void OnStarted() override {
    Update([](Telling& told) { ++told.started; });
  }
  void OnCompleted() override {
    Update([](Telling& told) { ++told.completed; });
  }
  void OnStopped(CaptureStop stop) override {
    Update([&](Telling& told) { told.stops.push_back(stop); });
  }
  void OnWritten(std::size_t bytes) override {
    Update([&](Telling& told) { told.written += bytes; });
  }
  void OnQueued(std::size_t records) override {
    Update([&](Telling& told) {
      told.queued = records;
      told.most_queued = std::max(told.most_queued, records);
    });
  }
  void OnDirectory(CaptureDirectoryUsage usage) override {
    Update([&](Telling& told) { told.directory = usage; });
  }

  [[nodiscard]] Telling Get() const {
    const std::scoped_lock lock(mutex_);
    return told_;
  }

 private:
  template <typename Change>
  void Update(Change change) {
    const std::scoped_lock lock(mutex_);
    change(told_);
  }

  mutable std::mutex mutex_;
  Telling told_;
};

// Each health a Capturer entered, in order, from whichever thread it entered it on.
class HealthLog final : public CaptureObserver {
 public:
  void OnHealth(CaptureHealth health) override {
    const std::scoped_lock lock(mutex_);
    entered_.push_back(health);
  }

  [[nodiscard]] std::vector<CaptureHealth> Entered() {
    const std::scoped_lock lock(mutex_);
    return entered_;
  }

 private:
  std::mutex mutex_;
  std::vector<CaptureHealth> entered_;
};

// Whether failure's description names text.
bool Names(const augusta::failure::Failure& failure, const std::string& text) {
  return DescribeFailure(failure).find(text) != std::string::npos;
}

class CapturerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Named after the running test: ctest runs every test in a process of its
    // own, possibly in parallel, so no other test removes it while in use.
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    std::string name = std::string("augusta_capturer_test_") + test.test_suite_name() + "_" + test.name();
    std::ranges::replace(name, '/', '_');
    directory_ = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(directory_);
    std::filesystem::create_directories(directory_);
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  // Every file in the directory, by name.
  [[nodiscard]] std::vector<std::filesystem::path> Files() const {
    std::vector<std::filesystem::path> files(std::filesystem::directory_iterator(directory_), {});
    std::ranges::sort(files);
    return files;
  }

  [[nodiscard]] static capture::Capture Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    auto read = ReadCapture(in);
    EXPECT_TRUE(read.has_value());
    return read.value_or(capture::Capture{});
  }

  std::filesystem::path directory_;
};

// Requirements: US-21
TEST_F(CapturerTest, AMatchPlayedToItsEndReadsBackAsPlayed) {
  {
    Capturer capturer(directory_, Header());
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.Command(kFirstTick, EntityId{70}, Walk(kFirstTick - 2));
    capturer.Command(kFirstTick + 1, EntityId{80}, Walk(kFirstTick));
    capturer.Death(kFirstTick + 5, EntityId{80}, EntityId{70});
    capturer.Leave(kFirstTick + 6, EntityId{80});
    capturer.EndMatch(kFirstTick + 6, SessionId{7});
  }

  const std::vector<std::filesystem::path> files = Files();
  ASSERT_EQ(files.size(), 1U);
  EXPECT_EQ(files.front().filename(), CaptureFileName(kStarted, 1));
  capture::Header header = Header();
  header.started = kStarted;
  const Command first = Walk(0);
  const capture::Capture expected{
      .header = header,
      .joins = {{.session = 7, .character = "soldier", .spawn = {1, 0, 2}},
                {.session = 8, .character = "sniper", .spawn = {-3, 0, 4}}},
      .events = {{.offset = 0, .event = capture::Command{.player = 1, .seen_offset = -2, .command = first}},
                 {.offset = 1, .event = capture::Command{.player = 2, .seen_offset = 0, .command = first}},
                 {.offset = 5, .event = capture::Death{.victim = 2, .killer = 1}},
                 {.offset = 6, .event = capture::Leave{.player = 2}},
                 {.offset = 6, .event = capture::MatchEnd{.winner = 1}}},
      .torn = false,
  };
  EXPECT_EQ(Read(files.front()), expected);
}

TEST_F(CapturerTest, ADrawEndsWithNoWinner) {
  {
    Capturer capturer(directory_, Header());
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.EndMatch(kFirstTick, std::nullopt);
  }

  const capture::Capture read = Read(Files().front());
  ASSERT_FALSE(read.events.empty());
  EXPECT_EQ(read.events.back(),
            (capture::AtOffset<capture::Event>{.offset = 0, .event = capture::MatchEnd{.winner = std::nullopt}}));
}

// Requirements: US-21
TEST_F(CapturerTest, EachMatchIsAFileOfItsOwn) {
  {
    Capturer capturer(directory_, Header());
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.EndMatch(kFirstTick + 10, std::nullopt);
    capturer.StartMatch(TwoPlayers(), kFirstTick + 100, kStarted + std::chrono::seconds{5});
    capturer.Command(kFirstTick + 103, EntityId{70}, Walk(kFirstTick + 101));
    capturer.EndMatch(kFirstTick + 110, SessionId{8});
  }

  const std::vector<std::filesystem::path> files = Files();
  ASSERT_EQ(files.size(), 2U);
  const capture::Capture second = Read(files.back());
  ASSERT_EQ(second.joins.size(), 2U);
  ASSERT_EQ(second.events.size(), 2U);
  EXPECT_EQ(second.events.front(),
            (capture::AtOffset<capture::Event>{
                .offset = 3, .event = capture::Command{.player = 1, .seen_offset = 1, .command = Walk(0)}}));
}

// Requirements: US-21
TEST_F(CapturerTest, NothingIsWrittenWithoutAMatch) {
  {
    const Capturer capturer(directory_, Header());
  }

  EXPECT_TRUE(Files().empty());
}

TEST_F(CapturerTest, EventsOfBodiesNotInTheMatchAreNotCaptured) {
  {
    Capturer capturer(directory_, Header());
    capturer.Command(kFirstTick, EntityId{70}, Walk(0));
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.Command(kFirstTick, EntityId{99}, Walk(0));
    capturer.Leave(kFirstTick, EntityId{99});
    capturer.EndMatch(kFirstTick, std::nullopt);
    capturer.Command(kFirstTick + 1, EntityId{70}, Walk(0));
  }

  // Two Joins and the Match end.
  const capture::Capture read = Read(Files().front());
  EXPECT_EQ(read.joins.size(), 2U);
  EXPECT_EQ(read.events.size(), 1U);
}

// Requirements: US-21
TEST_F(CapturerTest, AFailedWriteStopsTheCaptureKeepingEveryRecordBeforeIt) {
  Faults faults;
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.faults = &faults});
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.WaitUntilWritten();
    faults.Arm(Site::kCaptureWrite, "disk full", Faults::kEveryTime);
    capturer.Command(kFirstTick, EntityId{70}, Walk(0));
    capturer.Command(kFirstTick + 1, EntityId{70}, Walk(0));
    capturer.EndMatch(kFirstTick + 1, std::nullopt);
  }

  const capture::Capture read = Read(Files().front());
  // The header and both Joins, written before the disk failed.
  EXPECT_EQ(read.joins.size(), 2U);
  EXPECT_TRUE(read.events.empty());
  EXPECT_FALSE(read.torn);
}

TEST_F(CapturerTest, TheMatchAfterAStoppedCaptureIsCapturedAfresh) {
  Faults faults;
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.faults = &faults});
    faults.Arm(Site::kCaptureWrite, "disk full", 1);
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.EndMatch(kFirstTick, std::nullopt);
    capturer.StartMatch(TwoPlayers(), kFirstTick + 10, kStarted + std::chrono::seconds{1});
    capturer.EndMatch(kFirstTick + 10, std::nullopt);
  }

  const std::vector<std::filesystem::path> files = Files();
  ASSERT_EQ(files.size(), 2U);
  const capture::Capture second = Read(files.back());
  EXPECT_EQ(second.joins.size(), 2U);
  EXPECT_EQ(second.events.size(), 1U);
}

TEST_F(CapturerTest, ARecordThatFindsTheQueueFullStopsTheCapture) {
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.capacity = 0});
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.EndMatch(kFirstTick, std::nullopt);
  }

  // The header alone: the first Join found no room.
  const capture::Capture read = Read(Files().front());
  EXPECT_TRUE(read.joins.empty());
  EXPECT_TRUE(read.events.empty());
}

// Requirements: US-21
TEST_F(CapturerTest, AnOptionalCaptureThatFailsToWriteDegradesAndCapturesTheNextMatchAfresh) {
  Faults faults;
  HealthLog health;
  {
    Capturer capturer(directory_, Header(),
                      CaptureOptions{.mode = CaptureMode::kOptional, .faults = &faults, .observer = &health});
    faults.Arm(Site::kCaptureWrite, "disk full", 1);
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.EndMatch(kFirstTick, std::nullopt);
    capturer.WaitUntilWritten();

    EXPECT_EQ(capturer.Health(), CaptureHealth::kDegraded);
    const std::optional<augusta::failure::Failure> lost = capturer.Loss();
    ASSERT_TRUE(lost.has_value());
    EXPECT_EQ(lost->code, Code::kCaptureWriteFailed);
    EXPECT_EQ(DispositionOf(lost->code), Disposition::kSubsystem);
    EXPECT_TRUE(Names(*lost, "match=1 step=write")) << DescribeFailure(*lost);
    EXPECT_EQ(lost->detail, "disk full");

    capturer.StartMatch(TwoPlayers(), kFirstTick + 10, kStarted + std::chrono::seconds{1});
    capturer.EndMatch(kFirstTick + 10, std::nullopt);
  }

  const std::vector<std::filesystem::path> files = Files();
  ASSERT_EQ(files.size(), 2U);
  const capture::Capture second = Read(files.back());
  EXPECT_EQ(second.joins.size(), 2U);
  EXPECT_EQ(second.events.size(), 1U);
  EXPECT_EQ(health.Entered(),
            (std::vector<CaptureHealth>{CaptureHealth::kEnabled, CaptureHealth::kDegraded, CaptureHealth::kStopped}));
}

TEST_F(CapturerTest, AnOptionalCaptureWhoseFlushFailsDegradesOnAFlushFailure) {
  Faults faults;
  Capturer capturer(directory_, Header(), CaptureOptions{.mode = CaptureMode::kOptional, .faults = &faults});
  faults.Arm(Site::kCaptureFlush, "flush refused", 1);
  capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
  capturer.WaitUntilWritten();

  EXPECT_EQ(capturer.Health(), CaptureHealth::kDegraded);
  const std::optional<augusta::failure::Failure> lost = capturer.Loss();
  ASSERT_TRUE(lost.has_value());
  EXPECT_EQ(lost->code, Code::kCaptureFlushFailed);
  EXPECT_TRUE(Names(*lost, "match=1 step=flush")) << DescribeFailure(*lost);
}

TEST_F(CapturerTest, AnOptionalCaptureKeepsItsFirstLossOnly) {
  Faults faults;
  Capturer capturer(directory_, Header(), CaptureOptions{.mode = CaptureMode::kOptional, .faults = &faults});
  faults.Arm(Site::kCaptureWrite, "disk full", Faults::kEveryTime);
  capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
  capturer.EndMatch(kFirstTick, std::nullopt);
  capturer.StartMatch(TwoPlayers(), kFirstTick + 10, kStarted + std::chrono::seconds{1});
  capturer.EndMatch(kFirstTick + 10, std::nullopt);
  capturer.WaitUntilWritten();

  EXPECT_EQ(capturer.Health(), CaptureHealth::kDegraded);
  ASSERT_TRUE(capturer.Loss().has_value());
  EXPECT_TRUE(Names(*capturer.Loss(), "match=1 ")) << DescribeFailure(*capturer.Loss());
}

// Requirements: US-21
TEST_F(CapturerTest, AStrictCaptureThatFailsToWriteIsARuntimeFailureAndCapturesNothingMore) {
  Faults faults;
  HealthLog health;
  {
    Capturer capturer(directory_, Header(),
                      CaptureOptions{.mode = CaptureMode::kStrict, .faults = &faults, .observer = &health});
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.WaitUntilWritten();
    faults.Arm(Site::kCaptureWrite, "disk full", 1);
    capturer.Command(kFirstTick, EntityId{70}, Walk(0));
    capturer.WaitUntilWritten();

    EXPECT_EQ(capturer.Health(), CaptureHealth::kStopped);
    const std::optional<augusta::failure::Failure> lost = capturer.Loss();
    ASSERT_TRUE(lost.has_value());
    EXPECT_EQ(lost->code, Code::kStrictCaptureFailed);
    EXPECT_EQ(DispositionOf(lost->code), Disposition::kRuntime);
    EXPECT_TRUE(Names(*lost, "match=1 step=write")) << DescribeFailure(*lost);
    EXPECT_EQ(lost->detail, "disk full");

    capturer.EndMatch(kFirstTick + 1, std::nullopt);
    capturer.StartMatch(TwoPlayers(), kFirstTick + 10, kStarted + std::chrono::seconds{1});
    capturer.EndMatch(kFirstTick + 10, std::nullopt);
  }

  // The first Match's header and Joins, written before the disk failed, and no second file.
  const std::vector<std::filesystem::path> files = Files();
  ASSERT_EQ(files.size(), 1U);
  EXPECT_EQ(Read(files.front()).joins.size(), 2U);
  EXPECT_TRUE(Read(files.front()).events.empty());
  EXPECT_EQ(health.Entered(), (std::vector<CaptureHealth>{CaptureHealth::kEnabled, CaptureHealth::kStopped}));
}

TEST_F(CapturerTest, AStrictCaptureWhoseFlushFailsIsARuntimeFailure) {
  Faults faults;
  Capturer capturer(directory_, Header(), CaptureOptions{.mode = CaptureMode::kStrict, .faults = &faults});
  faults.Arm(Site::kCaptureFlush, "flush refused", 1);
  capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
  capturer.WaitUntilWritten();

  EXPECT_EQ(capturer.Health(), CaptureHealth::kStopped);
  const std::optional<augusta::failure::Failure> lost = capturer.Loss();
  ASSERT_TRUE(lost.has_value());
  EXPECT_EQ(lost->code, Code::kStrictCaptureFailed);
  EXPECT_TRUE(Names(*lost, "match=1 step=flush")) << DescribeFailure(*lost);
}

TEST_F(CapturerTest, AStrictCaptureWhoseQueueIsFullFailsAtOnce) {
  Capturer capturer(directory_, Header(), CaptureOptions{.mode = CaptureMode::kStrict, .capacity = 0});
  capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);

  // Known on the caller's thread, with no wait for the writer.
  EXPECT_EQ(capturer.Health(), CaptureHealth::kStopped);
  ASSERT_TRUE(capturer.Loss().has_value());
  EXPECT_EQ(capturer.Loss()->code, Code::kStrictCaptureFailed);
  EXPECT_TRUE(Names(*capturer.Loss(), "step=queue_full")) << DescribeFailure(*capturer.Loss());
}

// Requirements: US-21
TEST_F(CapturerTest, AnOptionalCaptureWhoseQueueIsFullIsASlowDiskNotAFailedWrite) {
  Capturer capturer(directory_, Header(), CaptureOptions{.mode = CaptureMode::kOptional, .capacity = 0});
  capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);

  EXPECT_EQ(capturer.Health(), CaptureHealth::kDegraded);
  ASSERT_TRUE(capturer.Loss().has_value());
  EXPECT_EQ(capturer.Loss()->code, Code::kCaptureQueueFull);
  EXPECT_EQ(DispositionOf(capturer.Loss()->code), Disposition::kSubsystem);
  EXPECT_TRUE(Names(*capturer.Loss(), "code=capture_queue_full")) << DescribeFailure(*capturer.Loss());
}

TEST(LostCaptureTest, AnOptionalCapturesLossIsNamedForTheStepItWasLostAt) {
  const auto code_at = [](CaptureStep step) { return LostCapture(CaptureMode::kOptional, 3, step, {}).code; };
  EXPECT_EQ(code_at(CaptureStep::kCreate), Code::kCaptureWriteFailed);
  EXPECT_EQ(code_at(CaptureStep::kWrite), Code::kCaptureWriteFailed);
  EXPECT_EQ(code_at(CaptureStep::kFlush), Code::kCaptureFlushFailed);
  EXPECT_EQ(code_at(CaptureStep::kQueueFull), Code::kCaptureQueueFull);
  EXPECT_EQ(code_at(CaptureStep::kRecordTooLong), Code::kCaptureRecordTooLong);
  for (const CaptureStep step : {CaptureStep::kCreate, CaptureStep::kWrite, CaptureStep::kFlush,
                                 CaptureStep::kQueueFull, CaptureStep::kRecordTooLong}) {
    EXPECT_EQ(DispositionOf(code_at(step)), Disposition::kSubsystem);
  }
}

TEST(LostCaptureTest, AStrictCapturesLossIsTheRuntimesWhateverItsStep) {
  for (const CaptureStep step : {CaptureStep::kCreate, CaptureStep::kWrite, CaptureStep::kFlush,
                                 CaptureStep::kQueueFull, CaptureStep::kRecordTooLong}) {
    EXPECT_EQ(LostCapture(CaptureMode::kStrict, 3, step, {}).code, Code::kStrictCaptureFailed);
  }
}

TEST(LostCaptureTest, ALossNamesItsMatchStepAndDetail) {
  EXPECT_EQ(DescribeFailure(LostCapture(CaptureMode::kOptional, 3, CaptureStep::kRecordTooLong, "300 bytes")),
            "code=capture_record_too_long disposition=subsystem match=3 step=record_too_long detail=\"300 bytes\"");
}

// Requirements: US-21
TEST_F(CapturerTest, AFinishedCaptureIsStoppedAndCapturesNothingMore) {
  HealthLog health;
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.mode = CaptureMode::kOptional, .observer = &health});
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.Command(kFirstTick, EntityId{70}, Walk(0));
    capturer.Finish();

    EXPECT_EQ(capturer.Health(), CaptureHealth::kStopped);
    EXPECT_FALSE(capturer.Loss().has_value());
    capturer.Command(kFirstTick + 1, EntityId{70}, Walk(0));
    capturer.StartMatch(TwoPlayers(), kFirstTick + 10, kStarted + std::chrono::seconds{1});
    capturer.EndMatch(kFirstTick + 10, std::nullopt);
  }

  // The first Match up to the Finish: both Joins and its one Command; no second file.
  const std::vector<std::filesystem::path> files = Files();
  ASSERT_EQ(files.size(), 1U);
  EXPECT_EQ(Read(files.front()).joins.size(), 2U);
  EXPECT_EQ(Read(files.front()).events.size(), 1U);
  EXPECT_EQ(health.Entered(), (std::vector<CaptureHealth>{CaptureHealth::kEnabled, CaptureHealth::kStopped}));
}

TEST_F(CapturerTest, ADegradedCaptureIsStoppedOnceFinishedKeepingItsLoss) {
  Faults faults;
  HealthLog health;
  Capturer capturer(directory_, Header(),
                    CaptureOptions{.mode = CaptureMode::kOptional, .faults = &faults, .observer = &health});
  faults.Arm(Site::kCaptureWrite, "disk full", 1);
  capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
  capturer.Finish();

  EXPECT_EQ(capturer.Health(), CaptureHealth::kStopped);
  ASSERT_TRUE(capturer.Loss().has_value());
  EXPECT_EQ(capturer.Loss()->code, Code::kCaptureWriteFailed);
  EXPECT_EQ(health.Entered(),
            (std::vector<CaptureHealth>{CaptureHealth::kEnabled, CaptureHealth::kDegraded, CaptureHealth::kStopped}));
}

// A record the codec cannot carry is the server's own bug, not the capture's
// loss: in either mode it is the runtime's broken invariant (ADR-0033), and
// nothing of it is written.
TEST_F(CapturerTest, ARecordTheCodecCannotCarryIsABrokenInvariantInEitherMode) {
  for (const CaptureMode mode : {CaptureMode::kOptional, CaptureMode::kStrict}) {
    const std::filesystem::path directory = directory_ / (mode == CaptureMode::kStrict ? "strict" : "optional");
    std::filesystem::create_directories(directory);
    std::vector<CaptureEntrant> players = TwoPlayers();
    players[1].character = std::string(1000, 'x');
    {
      Capturer capturer(directory, Header(), CaptureOptions{.mode = mode});
      capturer.StartMatch(players, kFirstTick, kStarted);
      capturer.EndMatch(kFirstTick, std::nullopt);
      capturer.WaitUntilWritten();

      ASSERT_TRUE(capturer.Failure().has_value());
      EXPECT_EQ(capturer.Failure()->code, Code::kInvariantViolated);
      EXPECT_EQ(DispositionOf(capturer.Failure()->code), Disposition::kRuntime);
      EXPECT_EQ(capturer.Health(), CaptureHealth::kEnabled);
      EXPECT_FALSE(capturer.Loss().has_value());
    }
    std::ifstream in(directory / CaptureFileName(kStarted, 1), std::ios::binary);
    const auto read = ReadCapture(in);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->joins.size(), 1U);
  }
}

// Requirements: US-21
TEST_F(CapturerTest, ACaptureThatLosesNothingIsTheSameInEitherMode) {
  std::vector<std::string> written;
  for (const CaptureMode mode : {CaptureMode::kOptional, CaptureMode::kStrict}) {
    const std::filesystem::path directory = directory_ / (mode == CaptureMode::kStrict ? "strict" : "optional");
    std::filesystem::create_directories(directory);
    HealthLog health;
    {
      Capturer capturer(directory, Header(), CaptureOptions{.mode = mode, .observer = &health});
      capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
      capturer.Command(kFirstTick, EntityId{70}, Walk(kFirstTick - 2));
      capturer.Death(kFirstTick + 5, EntityId{80}, EntityId{70});
      capturer.EndMatch(kFirstTick + 6, SessionId{7});
      capturer.WaitUntilWritten();
      EXPECT_EQ(capturer.Health(), CaptureHealth::kEnabled);
      EXPECT_FALSE(capturer.Loss().has_value());
    }
    EXPECT_EQ(health.Entered(), (std::vector<CaptureHealth>{CaptureHealth::kEnabled, CaptureHealth::kStopped}));
    std::ifstream in(directory / CaptureFileName(kStarted, 1), std::ios::binary);
    written.emplace_back(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  ASSERT_EQ(written.size(), 2U);
  EXPECT_FALSE(written.front().empty());
  EXPECT_EQ(written.front(), written.back());
}

#ifndef _WIN32
// A capture in the mode the test's parameter names.
class StalledDiskTest : public CapturerTest, public ::testing::WithParamInterface<CaptureMode> {};

// NFR-01: a disk that stalls holds up the writer, never the Simulation thread,
// in either mode: a strict capture fails the run, but only once its queue is full.
// The file is a FIFO with no reader yet, so the writer blocks opening it.
// POSIX only: Windows has no FIFO at a file path, and augustad runs on Linux.
// Requirements: NFR-01, US-21
TEST_P(StalledDiskTest, ADiskThatStallsNeverHoldsUpTheCallerAndStopsTheCaptureOnceItsQueueIsFull) {
  const std::filesystem::path fifo = directory_ / CaptureFileName(kStarted, 1);
  ASSERT_EQ(mkfifo(fifo.c_str(), 0600), 0);
  constexpr std::size_t kCapacity = 4;
  std::string drained;
  std::thread reader;
  Told told;
  {
    Capturer capturer(directory_, Header(),
                      CaptureOptions{.mode = GetParam(), .capacity = kCapacity, .observer = &told});
    const auto before = std::chrono::steady_clock::now();
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    for (std::uint64_t tick = kFirstTick; tick < kFirstTick + 100; ++tick) {
      capturer.Command(tick, EntityId{70}, Walk(tick));
    }
    capturer.EndMatch(kFirstTick + 100, std::nullopt);
    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::seconds{1});
    ASSERT_TRUE(capturer.Loss().has_value());
    EXPECT_EQ(capturer.Loss()->code,
              GetParam() == CaptureMode::kStrict ? Code::kStrictCaptureFailed : Code::kCaptureQueueFull);
    EXPECT_TRUE(Names(*capturer.Loss(), "step=queue_full")) << DescribeFailure(*capturer.Loss());
    // Unblocks the writer, which then writes what it queued before the stop.
    reader = std::thread([&] {
      std::ifstream in(fifo, std::ios::binary);
      drained.assign(std::istreambuf_iterator<char>(in), {});
    });
  }
  reader.join();

  std::istringstream in(drained, std::ios::binary);
  const auto read = ReadCapture(in);
  ASSERT_TRUE(read.has_value());
  // Both Joins and the Commands that found room; nothing after the stop.
  EXPECT_EQ(read->joins.size() + read->events.size(), kCapacity);
  EXPECT_EQ(told.Get().most_queued, kCapacity);
  EXPECT_EQ(told.Get().stops, std::vector{CaptureStop::kQueueFull});
}

// Requirements: NFR-07
TEST_F(CapturerTest, TheObserverIsToldOfACaptureFromItsStartToItsEnd) {
  Told told;
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.observer = &told});
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.Command(kFirstTick, EntityId{70}, Walk(0));
    capturer.EndMatch(kFirstTick + 1, std::nullopt);
  }

  const std::uintmax_t size = std::filesystem::file_size(Files().front());
  const Telling telling = told.Get();
  EXPECT_EQ(telling.states, (std::vector{CaptureState::kIdle, CaptureState::kCapturing, CaptureState::kIdle}));
  EXPECT_EQ(telling.started, 1U);
  EXPECT_EQ(telling.completed, 1U);
  EXPECT_TRUE(telling.stops.empty());
  EXPECT_EQ(telling.written, size);
  EXPECT_EQ(telling.queued, 0U);
  EXPECT_GT(telling.most_queued, 0U);
  EXPECT_EQ(telling.directory, (CaptureDirectoryUsage{.files = 1, .bytes = size}));
}

// Requirements: NFR-07
TEST_F(CapturerTest, TheDirectoryIsScannedForCapturesAtEachMatchStart) {
  // Captures are the files named as captures that start with a capture's magic;
  // one under the retired .capture extension is not.
  const std::string extension(capture::kExtension);
  const capture::Capture earlier{.header = Header(), .joins = {}, .events = {}, .torn = false};
  for (const std::string& name : {"20260101T000000000Z-0001" + extension, "20260101T000000000Z-0002" + extension,
                                  std::string("20260101T000000000Z-0004.capture")}) {
    std::ofstream out(directory_ / name, std::ios::binary);
    ASSERT_TRUE(augusta::capture::WriteCapture(out, earlier).has_value());
  }
  std::ofstream(directory_ / ("20260101T000000000Z-0003" + extension), std::ios::binary) << "no magic";
  std::ofstream(directory_ / "notes.txt", std::ios::binary) << "not a capture";
  std::filesystem::create_directory(directory_ / ("nested" + extension));
  Told told;
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.observer = &told});
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.EndMatch(kFirstTick, std::nullopt);
  }

  const std::uintmax_t earlier_size = std::filesystem::file_size(directory_ / ("20260101T000000000Z-0001" + extension));
  const std::uintmax_t size = std::filesystem::file_size(directory_ / CaptureFileName(kStarted, 1));
  EXPECT_EQ(told.Get().directory, (CaptureDirectoryUsage{.files = 3, .bytes = (2 * earlier_size) + size}));
}

// Requirements: NFR-07
TEST_F(CapturerTest, AStopIsToldWithItsReasonAndHoldsUntilTheMatchEnds) {
  Told told;
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.capacity = 0, .observer = &told});
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.EndMatch(kFirstTick, std::nullopt);
  }

  const Telling telling = told.Get();
  EXPECT_EQ(telling.stops, std::vector{CaptureStop::kQueueFull});
  EXPECT_EQ(telling.states,
            (std::vector{CaptureState::kIdle, CaptureState::kCapturing, CaptureState::kStopped, CaptureState::kIdle}));
  EXPECT_EQ(telling.started, 1U);
  EXPECT_EQ(telling.completed, 0U);
}

// Requirements: NFR-07
TEST_F(CapturerTest, AFailedWriteIsToldAsWriteFailed) {
  Faults faults;
  Told told;
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.faults = &faults, .observer = &told});
    faults.Arm(Site::kCaptureWrite, "disk full", 1);
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.WaitUntilWritten();
    EXPECT_EQ(told.Get().states.back(), CaptureState::kStopped);
    capturer.EndMatch(kFirstTick, std::nullopt);
  }

  const Telling telling = told.Get();
  EXPECT_EQ(telling.stops, std::vector{CaptureStop::kWriteFailed});
  EXPECT_EQ(telling.states.back(), CaptureState::kIdle);
  EXPECT_EQ(telling.completed, 0U);
}

INSTANTIATE_TEST_SUITE_P(EitherMode, StalledDiskTest, ::testing::Values(CaptureMode::kOptional, CaptureMode::kStrict),
                         [](const ::testing::TestParamInfo<CaptureMode>& info) {
                           return info.param == CaptureMode::kStrict ? std::string("Strict") : std::string("Optional");
                         });
#endif

}  // namespace
