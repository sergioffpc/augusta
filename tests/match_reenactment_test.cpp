#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "augusta/link.h"
#include "augusta/match.h"
#include "augusta/math.h"

// The Match's side of the reenactments debug tool (ADR-0050): a match that
// takes Reenact requests. Built only into a Development build's Match tests
// (ADR-0074); match_test.cpp has the refusal every build makes.
namespace {

using augusta::host::JoinRefusal;
using augusta::host::JoinRequest;
using augusta::host::Match;
using augusta::host::MatchConfig;
using augusta::link::PeerId;
using augusta::math::Vec3;

constexpr const char* kVersion = "1.2.3";
constexpr const char* kCharacter = "soldier";

PeerId Peer(std::uint32_t number) { return static_cast<PeerId>(number); }

JoinRequest Request(std::string version) {
  return JoinRequest{.engine_version = std::move(version), .client_pack = {}, .character = kCharacter};
}

// A Captured player's Reenact request: a Join request that names its spawn.
JoinRequest ReenactRequest(std::string version, const Vec3& spawn) {
  JoinRequest request = Request(std::move(version));
  request.spawn = spawn;
  return request;
}

// A match of player_count offering kCharacter alone, that takes Reenact requests.
MatchConfig ReenactingConfig(std::size_t player_count) {
  return MatchConfig{.engine_version = kVersion,
                     .characters = {kCharacter},
                     .player_count = player_count,
                     .pause_ticks = 0,
                     .reenactments = true};
}

// Requirements: US-21
TEST(MatchTest, AReenactRequestIsCheckedAsAJoinRequestIs) {
  Match match(ReenactingConfig(1));

  EXPECT_EQ(match.Join(Peer(1), ReenactRequest("other", Vec3())).error(), JoinRefusal::kVersionMismatch);
  JoinRequest unknown = ReenactRequest(kVersion, Vec3());
  unknown.character = "characters/nobody";
  EXPECT_EQ(match.Join(Peer(2), unknown).error(), JoinRefusal::kUnknownCharacter);
  ASSERT_TRUE(match.Join(Peer(3), ReenactRequest(kVersion, Vec3())).has_value());
  EXPECT_EQ(match.Join(Peer(4), ReenactRequest(kVersion, Vec3())).error(), JoinRefusal::kLobbyFull);
}

// Requirements: US-21
TEST(MatchTest, AMatchStartNamesTheSpawnOfEachPlayerAdmittedThroughAReenactRequestAndNoOneElses) {
  Match match(ReenactingConfig(3));
  ASSERT_TRUE(match.Join(Peer(1), Request(kVersion)).has_value());
  ASSERT_TRUE(match.Join(Peer(2), ReenactRequest(kVersion, Vec3(4.0F, 0.0F, -2.0F))).has_value());
  ASSERT_TRUE(match.Join(Peer(3), ReenactRequest(kVersion, Vec3(-6.0F, 1.0F, 8.0F))).has_value());
  for (std::uint32_t i = 1; i <= 3; ++i) {
    match.Ready(Peer(i), match.GetRoster().version);
  }

  const auto start = match.TryStart();

  ASSERT_TRUE(start.has_value());
  ASSERT_EQ(start->players.size(), 3U);
  EXPECT_FALSE(start->players[0].spawn.has_value());
  EXPECT_EQ(start->players[1].spawn, Vec3(4.0F, 0.0F, -2.0F));
  EXPECT_EQ(start->players[2].spawn, Vec3(-6.0F, 1.0F, 8.0F));
}

}  // namespace
