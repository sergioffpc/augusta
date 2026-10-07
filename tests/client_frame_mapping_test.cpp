#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/effects.h"
#include "augusta/harness.h"
#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/presentation.h"
#include "frame_mapping.h"

// Unit tests for the conversions ClientRuntime makes at its edges. Links no
// Falcor or window: they only use renderer.h's plain types.
namespace {

using augusta::client::CharactersOf;
using augusta::client::CombatEffectsOf;
using augusta::client::MatchEndOf;
using augusta::client::SnapshotOf;
using augusta::client::WithSeenTime;
using augusta::harness::EntityId;
using augusta::harness::SessionId;
using augusta::math::Vec3;

constexpr float kTolerance = 1e-5F;

augusta::harness::MatchStart TwoPlayerMatch() {
  return {.players = {{.session = SessionId{7}, .entity = EntityId{70}, .character = "medic"},
                      {.session = SessionId{3}, .entity = EntityId{30}, .character = "sniper"}}};
}

TEST(SnapshotOfTest, IsNoneOutsideAMatch) {
  augusta::harness::ServerView view;
  view.accepted = augusta::harness::Admission{.tick_rate_hz = 60, .character = ""};

  EXPECT_FALSE(SnapshotOf(view).has_value());
}

TEST(SnapshotOfTest, CarriesEveryBodyAtTheServersTickDuration) {
  augusta::harness::ServerView view;
  view.accepted = augusta::harness::Admission{.tick_rate_hz = 50, .character = ""};
  view.authoritative = augusta::harness::AuthoritativeState{
      .tick = 12, .bodies = {{.entity = EntityId{4}, .yaw = 1.5F}, {.entity = EntityId{9}}}};

  const auto snapshot = SnapshotOf(view);

  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(snapshot->tick, 12U);
  EXPECT_DOUBLE_EQ(snapshot->tick_duration, 0.02);
  ASSERT_EQ(snapshot->bodies.size(), 2U);
  EXPECT_EQ(snapshot->bodies[0].entity, augusta::presentation::EntityId{4});
  EXPECT_FLOAT_EQ(snapshot->bodies[0].yaw, 1.5F);
  EXPECT_EQ(snapshot->bodies[1].entity, augusta::presentation::EntityId{9});
}

TEST(CharactersOfTest, AreEveryPlayersCharacterInSessionOrder) {
  const auto characters = CharactersOf(TwoPlayerMatch());

  ASSERT_EQ(characters.size(), 2U);
  EXPECT_EQ(characters[0].entity, augusta::presentation::EntityId{30});
  EXPECT_EQ(characters[0].character, "sniper");
  EXPECT_EQ(characters[1].entity, augusta::presentation::EntityId{70});
  EXPECT_EQ(characters[1].character, "medic");
}

TEST(CharactersOfTest, AreNoneBeforeTheFirstMatch) { EXPECT_TRUE(CharactersOf(std::nullopt).empty()); }

TEST(MatchEndOfTest, NamesTheWinnerByTheBodyItPlayed) {
  augusta::harness::ServerView view;
  view.match_start = TwoPlayerMatch();
  view.match_end = augusta::harness::MatchEnd{.winner = SessionId{7}};

  const auto end = MatchEndOf(view);

  ASSERT_TRUE(end.has_value());
  EXPECT_EQ(end->winner, augusta::presentation::EntityId{70});
}

TEST(MatchEndOfTest, AWinnerMissingFromMatchStartIsNone) {
  augusta::harness::ServerView view;
  view.match_start = TwoPlayerMatch();
  view.match_end = augusta::harness::MatchEnd{.winner = SessionId{99}};

  const auto end = MatchEndOf(view);

  ASSERT_TRUE(end.has_value());
  EXPECT_FALSE(end->winner.has_value());
}

TEST(MatchEndOfTest, IsNoneWhileNoMatchHasEnded) {
  augusta::harness::ServerView view;
  view.match_start = TwoPlayerMatch();

  EXPECT_FALSE(MatchEndOf(view).has_value());
}

TEST(CombatEffectsOfTest, FadesEachEffectByItsAgeOverItsLifetime) {
  augusta::presentation::State state;
  state.impacts = {{.position = Vec3(1.0F, 0.0F, 0.0F), .age = augusta::presentation::kImpactSeconds / 4.0F}};
  state.muzzle_flashes = {{.position = Vec3(0.0F, 2.0F, 0.0F), .age = 0.0F}};
  state.tracers = {{.head = Vec3(0.0F, 0.0F, 1.0F), .tail = Vec3(0.0F, 0.0F, 0.0F)}};

  const auto effects = CombatEffectsOf(state);

  ASSERT_EQ(effects.impacts.size(), 1U);
  EXPECT_NEAR(effects.impacts[0].fade, 0.75F, kTolerance);
  EXPECT_EQ(effects.impacts[0].position, Vec3(1.0F, 0.0F, 0.0F));
  ASSERT_EQ(effects.muzzle_flashes.size(), 1U);
  EXPECT_NEAR(effects.muzzle_flashes[0].fade, 1.0F, kTolerance);
  ASSERT_EQ(effects.tracers.size(), 1U);
  EXPECT_EQ(effects.tracers[0].head, Vec3(0.0F, 0.0F, 1.0F));
}

TEST(WithSeenTimeTest, ReportsTheSeenTimeOfTheLastFrame) {
  const auto command = WithSeenTime({}, augusta::presentation::SeenTime{.tick = 41, .fraction = 0.5F});

  EXPECT_EQ(command.seen_tick, 41U);
  EXPECT_FLOAT_EQ(command.seen_fraction, 0.5F);
}

TEST(WithSeenTimeTest, ReportsNoneWithoutASeenTime) {
  const augusta::command::Command command = WithSeenTime({}, std::nullopt);

  EXPECT_EQ(command.seen_tick, 0U);
  EXPECT_FLOAT_EQ(command.seen_fraction, 0.0F);
}

}  // namespace
