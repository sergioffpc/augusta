#include "capture.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "augusta/command.h"
#include "augusta/faults.h"
#include "augusta/math.h"
#include "augusta/protocol.h"
#include "frames.h"
#include "match.h"
#include "wire.h"

// A Match capture (ADR-0050), written by a Capturer into a directory of its
// own and read back by ReadCapture, with no Host: the events are handed in by
// hand, as Host hands them on the Simulation thread.
namespace {

using augusta::command::Command;
using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::math::Vec3;
using augusta::server::Capture;
using augusta::server::CapturedCommand;
using augusta::server::CapturedDeath;
using augusta::server::CapturedJoin;
using augusta::server::CapturedLeave;
using augusta::server::CapturedMatchEnd;
using augusta::server::CaptureEntrant;
using augusta::server::CaptureError;
using augusta::server::CaptureFileName;
using augusta::server::CaptureHeader;
using augusta::server::CaptureOptions;
using augusta::server::Capturer;
using augusta::server::CaptureRecord;
using augusta::server::EntityId;
using augusta::server::ReadCapture;
using augusta::server::SessionId;

using Milliseconds = std::chrono::sys_time<std::chrono::milliseconds>;

// records as they are written: Command has no ==, and its record compares on the protocol's grids.
std::vector<augusta::protocol::CaptureRecordWire> AsWritten(const std::vector<CaptureRecord>& records) {
  std::vector<augusta::protocol::CaptureRecordWire> written;
  for (const CaptureRecord& record : records) {
    written.push_back(augusta::server::ToWire(record));
  }
  return written;
}

// 2026-10-09 10:15:00.123 UTC.
const Milliseconds kStarted{std::chrono::milliseconds{1'791'540'900'123}};
constexpr std::uint64_t kFirstTick = 1000;

CaptureHeader Header() {
  CaptureHeader header{.engine_version = "2.0.1", .tick_rate_hz = 60, .started = {}};
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

class CaptureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Named after the running test: ctest runs every test in a process of its
    // own, possibly in parallel, so no other test removes it while in use.
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    std::string name = std::string("augusta_capture_test_") + test.test_suite_name() + "_" + test.name();
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

  [[nodiscard]] static Capture Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    auto capture = ReadCapture(in);
    EXPECT_TRUE(capture.has_value());
    return capture.value_or(Capture{});
  }

  std::filesystem::path directory_;
};

TEST(CaptureFileNameTest, NamesTheMatchsStartInUtcThenItsNumber) {
  EXPECT_EQ(CaptureFileName(kStarted, 3), "20261009T101500123Z-0003.capture");
}

TEST(CaptureFileNameTest, OneServersNamesSortByWhenTheirMatchStarted) {
  const std::string first = CaptureFileName(kStarted, 9);
  const std::string second = CaptureFileName(kStarted + std::chrono::milliseconds{1}, 10);
  EXPECT_LT(first, second);
}

// Requirements: US-21
TEST_F(CaptureTest, AMatchPlayedToItsEndReadsBackAsPlayed) {
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
  const Capture capture = Read(files.front());
  CaptureHeader header = Header();
  header.started = kStarted;
  EXPECT_EQ(capture.header, header);
  EXPECT_FALSE(capture.torn);
  const Command first = Walk(0);
  const std::vector<CaptureRecord> expected{
      {.offset = 0,
       .event = CapturedJoin{.player = 1, .session = SessionId{7}, .character = "soldier", .spawn = {1, 0, 2}}},
      {.offset = 0,
       .event = CapturedJoin{.player = 2, .session = SessionId{8}, .character = "sniper", .spawn = {-3, 0, 4}}},
      {.offset = 0, .event = CapturedCommand{.player = 1, .seen_offset = -2, .command = first}},
      {.offset = 1, .event = CapturedCommand{.player = 2, .seen_offset = 0, .command = first}},
      {.offset = 5, .event = CapturedDeath{.victim = 2, .killer = 1}},
      {.offset = 6, .event = CapturedLeave{.player = 2}},
      {.offset = 6, .event = CapturedMatchEnd{.winner = 1}},
  };
  EXPECT_EQ(AsWritten(capture.records), AsWritten(expected));
}

TEST_F(CaptureTest, ADrawEndsWithNoWinner) {
  {
    Capturer capturer(directory_, Header());
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.EndMatch(kFirstTick, std::nullopt);
  }

  const Capture capture = Read(Files().front());
  ASSERT_FALSE(capture.records.empty());
  EXPECT_EQ(AsWritten({capture.records.back()}),
            AsWritten({CaptureRecord{.offset = 0, .event = CapturedMatchEnd{.winner = std::nullopt}}}));
}

// Requirements: US-21
TEST_F(CaptureTest, EachMatchIsAFileOfItsOwn) {
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
  const Capture second = Read(files.back());
  ASSERT_EQ(second.records.size(), 4U);
  EXPECT_EQ(AsWritten({second.records[2]}),
            AsWritten({CaptureRecord{.offset = 3,
                                     .event = CapturedCommand{.player = 1, .seen_offset = 1, .command = Walk(0)}}}));
}

// Requirements: US-21
TEST_F(CaptureTest, NothingIsWrittenWithoutAMatch) {
  {
    const Capturer capturer(directory_, Header());
  }

  EXPECT_TRUE(Files().empty());
}

TEST_F(CaptureTest, EventsOfBodiesNotInTheMatchAreNotCaptured) {
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
  EXPECT_EQ(Read(Files().front()).records.size(), 3U);
}

// Requirements: US-21
TEST_F(CaptureTest, AFailedWriteStopsTheCaptureKeepingEveryRecordBeforeIt) {
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

  const Capture capture = Read(Files().front());
  // The header and both Joins, written before the disk failed.
  EXPECT_EQ(capture.records.size(), 2U);
  EXPECT_FALSE(capture.torn);
}

TEST_F(CaptureTest, TheMatchAfterAStoppedCaptureIsCapturedAfresh) {
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
  EXPECT_EQ(Read(files.back()).records.size(), 3U);
}

TEST_F(CaptureTest, ARecordThatFindsTheQueueFullStopsTheCapture) {
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.capacity = 0});
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    capturer.EndMatch(kFirstTick, std::nullopt);
  }

  // The header alone: the first Join found no room.
  const Capture capture = Read(Files().front());
  EXPECT_TRUE(capture.records.empty());
}

#ifndef _WIN32
// NFR-01: a disk that stalls holds up the writer, never the Simulation thread.
// The file is a FIFO with no reader yet, so the writer blocks opening it.
// POSIX only: Windows has no FIFO at a file path, and augustad runs on Linux.
// Requirements: NFR-01, US-21
TEST_F(CaptureTest, ADiskThatStallsNeverHoldsUpTheCallerAndStopsTheCaptureOnceItsQueueIsFull) {
  const std::filesystem::path fifo = directory_ / CaptureFileName(kStarted, 1);
  ASSERT_EQ(mkfifo(fifo.c_str(), 0600), 0);
  constexpr std::size_t kCapacity = 4;
  std::string drained;
  std::thread reader;
  {
    Capturer capturer(directory_, Header(), CaptureOptions{.capacity = kCapacity});
    const auto before = std::chrono::steady_clock::now();
    capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
    for (std::uint64_t tick = kFirstTick; tick < kFirstTick + 100; ++tick) {
      capturer.Command(tick, EntityId{70}, Walk(tick));
    }
    capturer.EndMatch(kFirstTick + 100, std::nullopt);
    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::seconds{1});
    // Unblocks the writer, which then writes what it queued before the stop.
    reader = std::thread([&] {
      std::ifstream in(fifo, std::ios::binary);
      drained.assign(std::istreambuf_iterator<char>(in), {});
    });
  }
  reader.join();

  std::istringstream in(drained, std::ios::binary);
  const auto capture = ReadCapture(in);
  ASSERT_TRUE(capture.has_value());
  // Both Joins and the Commands that found room; nothing after the stop.
  EXPECT_EQ(capture->records.size(), kCapacity);
}
#endif

// A file of the capture written in one Match, for ReadCapture's own tests.
class ReadCaptureTest : public CaptureTest {
 protected:
  [[nodiscard]] std::string Bytes() {
    {
      Capturer capturer(directory_, Header());
      capturer.StartMatch(TwoPlayers(), kFirstTick, kStarted);
      capturer.Command(kFirstTick, EntityId{70}, Walk(0));
      capturer.EndMatch(kFirstTick + 1, std::nullopt);
    }
    std::ifstream in(Files().front(), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
  }

  static std::expected<Capture, CaptureError> ReadBytes(const std::string& bytes) {
    std::istringstream in(bytes, std::ios::binary);
    return ReadCapture(in);
  }
};

// Requirements: NFR-12
TEST_F(ReadCaptureTest, AFileWithoutTheMagicIsNoCapture) {
  std::string bytes = Bytes();
  bytes[0] = 'P';
  EXPECT_EQ(ReadBytes(bytes).error(), CaptureError::kNotACapture);
  EXPECT_EQ(ReadBytes("AUG").error(), CaptureError::kNotACapture);
}

// Requirements: US-21
TEST_F(ReadCaptureTest, ALastRecordCutShortIsDroppedAndReported) {
  const std::string bytes = Bytes();
  const auto torn = ReadBytes(bytes.substr(0, bytes.size() - 1));
  ASSERT_TRUE(torn.has_value());
  EXPECT_TRUE(torn->torn);
  EXPECT_EQ(torn->records.size(), ReadBytes(bytes)->records.size() - 1);
}

// Requirements: NFR-12
TEST_F(ReadCaptureTest, AnotherFormatVersionIsRefused) {
  std::string bytes = Bytes();
  // The header record's format version: after the magic, its 1-byte length and its type.
  bytes[augusta::protocol::kCaptureMagic.size() + 1 + 1] =
      static_cast<char>(augusta::protocol::kCaptureFormatVersion + 1);
  EXPECT_EQ(ReadBytes(bytes).error(), CaptureError::kUnsupported);
}

// Requirements: NFR-12
TEST_F(ReadCaptureTest, ARecordNamingAPlayerNoJoinDidIsRefused) {
  std::string bytes = Bytes();
  std::ostringstream out(bytes, std::ios::binary | std::ios::ate);
  // After the Match end, which nothing may follow, and of a player 9.
  augusta::server::WriteFrame(
      out,
      augusta::protocol::EncodeCaptureRecord(augusta::protocol::CapturedLeaveWire{.offset = 5, .player = 9}).value());
  EXPECT_EQ(ReadBytes(out.str()).error(), CaptureError::kMalformed);
}

}  // namespace
