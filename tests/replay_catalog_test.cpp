#include "replay_catalog.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "capture.h"
#include "match.h"

// Which captures a replay server replays (ADR-0051), from a directory of
// captures a Capturer wrote, as a live server's would be.
namespace {

using augusta::server::Capture;
using augusta::server::CaptureEntrant;
using augusta::server::CaptureHeader;
using augusta::server::Capturer;
using augusta::server::EntityId;
using augusta::server::ListingOf;
using augusta::server::ReplayCatalog;
using augusta::server::ReplayListing;
using augusta::server::ReplayTerms;
using augusta::server::SessionId;

constexpr std::uint8_t kTickRate = 60;

augusta::assets::PackHash PackOf(std::uint8_t byte) {
  augusta::assets::PackHash pack{};
  pack.fill(std::byte{byte});
  return pack;
}

ReplayTerms Terms() {
  return ReplayTerms{.server_pack = PackOf(1), .tick_rate_hz = kTickRate, .characters = {"soldier", "sniper"}};
}

// 2026-10-09 10:15:00.123 UTC.
const std::chrono::system_clock::time_point kStarted{std::chrono::milliseconds{1'791'540'900'123}};

class ReplayCatalogTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    root_ = std::filesystem::temp_directory_path() /
            (std::string("augusta_replay_catalog_") + test.name() + "_" + std::to_string(std::random_device{}()));
    directory_ = root_ / "captures";
    std::filesystem::create_directories(directory_);
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  // Writes one capture into the directory, of two players playing characters
  // for last_offset + 1 ticks, made on pack at tick_rate_hz; returns its name.
  std::string Write(std::uint8_t pack = 1, std::uint8_t tick_rate_hz = kTickRate,
                    const std::vector<std::string>& characters = {"soldier", "sniper"},
                    std::uint64_t last_offset = 30) {
    std::vector<CaptureEntrant> players;
    for (std::size_t i = 0; i < characters.size(); ++i) {
      players.push_back(CaptureEntrant{.session = SessionId{static_cast<std::uint32_t>(i + 1)},
                                       .entity = EntityId{static_cast<std::uint32_t>(i + 1)},
                                       .character = characters[i],
                                       .spawn = {}});
    }
    {
      Capturer capturer(directory_, CaptureHeader{.engine_version = "1.0.0",
                                                  .server_pack = PackOf(pack),
                                                  .client_pack = PackOf(2),
                                                  .tick_rate_hz = tick_rate_hz,
                                                  .started = {}});
      capturer.StartMatch(players, 100, kStarted + std::chrono::seconds(++written_));
      capturer.Command(100 + (last_offset / 2), EntityId{1}, augusta::command::Command{});
      capturer.EndMatch(100 + last_offset, std::nullopt);
    }
    // Its only Match, the first of its run: destroying it wrote the file.
    return augusta::server::CaptureFileName(
        std::chrono::floor<std::chrono::milliseconds>(kStarted + std::chrono::seconds(written_)), 1);
  }

  std::filesystem::path root_;
  std::filesystem::path directory_;
  int written_ = 0;
};

// Requirements: US-21
TEST_F(ReplayCatalogTest, ListsEachCaptureItReplaysWithItsStartLengthAndCharacters) {
  const std::string first = Write();
  const std::string second = Write(1, kTickRate, {"sniper"}, 9);

  const std::vector<ReplayListing> listed = ReplayCatalog(directory_, Terms()).List();

  ASSERT_EQ(listed.size(), 2U);
  EXPECT_EQ(listed[0].name, first);
  EXPECT_EQ(listed[0].started, std::chrono::floor<std::chrono::milliseconds>(kStarted + std::chrono::seconds(1)));
  EXPECT_EQ(listed[0].ticks, 31U);
  EXPECT_EQ(listed[0].tick_rate_hz, kTickRate);
  EXPECT_EQ(listed[0].characters, (std::vector<std::string>{"soldier", "sniper"}));
  EXPECT_EQ(listed[1].name, second);
  EXPECT_EQ(listed[1].ticks, 10U);
  EXPECT_EQ(listed[1].characters, (std::vector<std::string>{"sniper"}));
}

// A Replay needs the pack's Map, Characters and Game policy, at the rate its
// Parameters were loaded for.
// Requirements: US-21
TEST_F(ReplayCatalogTest, LeavesOutCapturesOfAnotherPackRateOrScenario) {
  const std::string replayable = Write();
  Write(3);
  Write(1, 30);
  Write(1, kTickRate, {"soldier", "medic"});

  const std::vector<ReplayListing> listed = ReplayCatalog(directory_, Terms()).List();

  ASSERT_EQ(listed.size(), 1U);
  EXPECT_EQ(listed[0].name, replayable);
}

TEST_F(ReplayCatalogTest, LeavesOutWhatIsNoCapture) {
  const std::string replayable = Write();
  std::ofstream(directory_ / "notes.capture") << "not a capture";
  std::ofstream(directory_ / "readme.txt") << "hello";
  std::filesystem::create_directories(directory_ / "nested.capture");

  const std::vector<ReplayListing> listed = ReplayCatalog(directory_, Terms()).List();

  ASSERT_EQ(listed.size(), 1U);
  EXPECT_EQ(listed[0].name, replayable);
}

TEST_F(ReplayCatalogTest, ADirectoryThatIsGoneListsNothing) {
  EXPECT_TRUE(ReplayCatalog(root_ / "missing", Terms()).List().empty());
}

// Requirements: US-21
TEST_F(ReplayCatalogTest, FindsACaptureByItsListedName) {
  const std::string name = Write(1, kTickRate, {"soldier", "sniper"}, 12);

  const std::optional<Capture> found = ReplayCatalog(directory_, Terms()).Find(name);

  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->header.tick_rate_hz, kTickRate);
  EXPECT_EQ(found->records.back().offset, 12U);
}

// A name is matched against the directory's listing, never opened as a path
// (ADR-0051): a capture outside the directory, however named, is no capture
// of its, and neither is one it lists but does not replay.
// Requirements: US-21
TEST_F(ReplayCatalogTest, ANameNotInTheListingIsUnknownAPathAmongThem) {
  const std::string name = Write();
  std::filesystem::copy_file(directory_ / name, root_ / "outside.capture");
  std::filesystem::create_directories(directory_ / "sub");
  std::filesystem::copy_file(directory_ / name, directory_ / "sub" / "inner.capture");
  const std::string other_pack = Write(3);
  const ReplayCatalog catalog(directory_, Terms());

  for (const std::string& unknown :
       {std::string("../outside.capture"), (root_ / "outside.capture").string(), std::string("sub/inner.capture"),
        "./" + name, std::string(""), std::string("."), std::string(".."), std::string("captures/") + name,
        "../captures/" + name, name + "/", other_pack, std::string("missing.capture")}) {
    EXPECT_FALSE(catalog.Find(unknown).has_value()) << unknown;
  }
  EXPECT_TRUE(catalog.Find(name).has_value());
}

// Requirements: US-21
TEST(ReplayListingTest, ACapturesLengthRunsToItsLastTick) {
  Capture capture;
  capture.header.server_pack = PackOf(1);
  capture.header.tick_rate_hz = kTickRate;
  capture.records = {
      augusta::server::CaptureRecord{
          .offset = 0,
          .event = augusta::server::CapturedJoin{.player = 1, .session = SessionId{4}, .character = "soldier"}},
      augusta::server::CaptureRecord{.offset = 59, .event = augusta::server::CapturedMatchEnd{}}};

  const std::optional<ReplayListing> listing = ListingOf("a.capture", capture, Terms());

  ASSERT_TRUE(listing.has_value());
  EXPECT_EQ(listing->ticks, 60U);
  EXPECT_EQ(listing->name, "a.capture");
}

TEST(ReplayListingTest, ACaptureWithNoPlayerIsNotReplayed) {
  Capture capture;
  capture.header.server_pack = PackOf(1);
  capture.header.tick_rate_hz = kTickRate;
  capture.torn = true;

  EXPECT_FALSE(ListingOf("a.capture", capture, Terms()).has_value());
}

}  // namespace
