#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/effects.h"
#include "augusta/harness.h"
#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/presentation.h"
#include "augusta/tick.h"
#include "frame_mapping.h"

// Unit tests for the conversions ClientRuntime makes at its edges. Links no
// GPU or window: they only use renderer.h's plain types.
namespace {

using augusta::client::CharactersOf;
using augusta::client::CombatEffectsOf;
using augusta::client::ConvertedServerView;
using augusta::client::MatchEndOf;
using augusta::client::ViewsOf;
using augusta::client::WithSeenTime;
using augusta::harness::EntityId;
using augusta::harness::SessionId;
using augusta::math::Vec3;

// A Session ID, a UUIDv7, told apart by its last byte.
constexpr SessionId Session(std::uint8_t last) {
  return SessionId::FromBytes(
      {0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD4, 0x70, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, last});
}

constexpr float kTolerance = 1e-5F;

augusta::harness::MatchStart TwoPlayerMatch() {
  return {.players = {{.session = Session(7), .entity = EntityId{70}, .character = "medic"},
                      {.session = Session(3), .entity = EntityId{30}, .character = "sniper"}}};
}

// Requirements: US-17
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
  view.match_end = augusta::harness::MatchEnd{.winner = Session(7)};

  const auto end = MatchEndOf(view);

  ASSERT_TRUE(end.has_value());
  EXPECT_EQ(end->winner, augusta::presentation::EntityId{70});
}

TEST(MatchEndOfTest, AWinnerMissingFromMatchStartIsNone) {
  augusta::harness::ServerView view;
  view.match_start = TwoPlayerMatch();
  view.match_end = augusta::harness::MatchEnd{.winner = Session(99)};

  const auto end = MatchEndOf(view);

  ASSERT_TRUE(end.has_value());
  EXPECT_FALSE(end->winner.has_value());
}

TEST(MatchEndOfTest, IsNoneWhileNoMatchHasEnded) {
  augusta::harness::ServerView view;
  view.match_start = TwoPlayerMatch();

  EXPECT_FALSE(MatchEndOf(view).has_value());
}

// Requirements: US-19
// Requirements: US-21
TEST(ViewsOfTest, AreEachPlayersPitchAndAdsOfTheNewestReplayView) {
  augusta::harness::ServerView view;
  view.replay_view = augusta::harness::ReplayView{
      .tick = 9,
      .players = {{.entity = EntityId{1}, .pitch = 0.25F, .ads = true}, {.entity = EntityId{2}, .pitch = -0.5F}}};

  const std::vector<augusta::presentation::PlayerView> views = ViewsOf(view);

  ASSERT_EQ(views.size(), 2U);
  EXPECT_EQ(views[0].entity, augusta::presentation::EntityId{1});
  EXPECT_FLOAT_EQ(views[0].pitch, 0.25F);
  EXPECT_TRUE(views[0].ads);
  EXPECT_FLOAT_EQ(views[1].pitch, -0.5F);
  EXPECT_FALSE(views[1].ads);
}

TEST(ViewsOfTest, AreNoneForAClientThatIsNoReplayViewer) {
  EXPECT_TRUE(ViewsOf(augusta::harness::ServerView{}).empty());
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

// A Server view in the matches_started-th match, at its Authoritative State of
// tick naming bodies.
// A view of match matches_started whose recent Authoritative State updates
// are recent, oldest first, the newest also its authoritative one.
augusta::harness::ServerView InMatch(std::uint32_t matches_started,
                                     std::vector<augusta::harness::AuthoritativeState> recent) {
  augusta::harness::ServerView view;
  view.accepted = augusta::harness::Admission{.tick_rate_hz = 50, .character = ""};
  view.match_start = TwoPlayerMatch();
  view.matches_started = matches_started;
  view.in_match = true;
  if (!recent.empty()) {
    view.authoritative = recent.back();
  }
  view.recent_authoritative = std::move(recent);
  return view;
}

// A view of match matches_started whose only update is of tick, with bodies.
augusta::harness::ServerView InMatch(std::uint32_t matches_started, augusta::tick::Tick tick,
                                     std::vector<augusta::harness::EntityBody> bodies) {
  return InMatch(matches_started, {{.tick = tick, .bodies = std::move(bodies)}});
}

std::vector<augusta::tick::Tick> TicksOf(std::span<const augusta::presentation::WorldSnapshot> updates) {
  std::vector<augusta::tick::Tick> ticks;
  for (const augusta::presentation::WorldSnapshot& update : updates) {
    ticks.push_back(update.tick);
  }
  return ticks;
}

TEST(ConvertedServerViewTest, IsNothingBeforeTheFirstView) {
  const ConvertedServerView converted;

  EXPECT_TRUE(converted.Updates().empty());
  EXPECT_TRUE(converted.Characters().empty());
}

TEST(ConvertedServerViewTest, IsNoUpdateBeforeTheFirstAuthoritativeState) {
  ConvertedServerView converted;

  converted.Update(InMatch(1, {}));

  EXPECT_TRUE(converted.Updates().empty());
}

TEST(ConvertedServerViewTest, CarriesTheNewestAuthoritativeStateAtTheServersTickDuration) {
  ConvertedServerView converted;

  converted.Update(InMatch(1, 12, {{.entity = EntityId{4}, .yaw = 1.5F}, {.entity = EntityId{9}}}));

  ASSERT_EQ(converted.Updates().size(), 1U);
  const augusta::presentation::WorldSnapshot& update = converted.Updates()[0];
  EXPECT_EQ(update.tick, 12U);
  EXPECT_DOUBLE_EQ(update.tick_duration, 0.02);
  ASSERT_EQ(update.bodies.size(), 2U);
  EXPECT_EQ(update.bodies[0].entity, augusta::presentation::EntityId{4});
  EXPECT_FLOAT_EQ(update.bodies[0].yaw, 1.5F);
}

// A render frame below the tick rate is handed every update that arrived since
// the last, not the newest alone.
// Requirements: US-17
TEST(ConvertedServerViewTest, CarriesEveryRecentUpdateOfTheServerViewOldestFirst) {
  ConvertedServerView converted;
  converted.Update(InMatch(1, {{.tick = 10, .bodies = {}}}));

  converted.Update(InMatch(1, {{.tick = 10, .bodies = {}},
                               {.tick = 11, .bodies = {}},
                               {.tick = 12, .bodies = {}},
                               {.tick = 13, .bodies = {}}}));

  EXPECT_EQ(TicksOf(converted.Updates()), (std::vector<augusta::tick::Tick>{10, 11, 12, 13}));
}

TEST(ConvertedServerViewTest, DropsTheUpdatesTheServerViewNoLongerKeeps) {
  ConvertedServerView converted;
  converted.Update(InMatch(1, {{.tick = 10, .bodies = {}}, {.tick = 11, .bodies = {}}}));

  converted.Update(InMatch(1, {{.tick = 11, .bodies = {}}, {.tick = 12, .bodies = {}}}));

  EXPECT_EQ(TicksOf(converted.Updates()), (std::vector<augusta::tick::Tick>{11, 12}));
}

// The Inbox publishes an Authoritative State only newer than the one it holds,
// so a view of the same match holds the same state at a tick already converted:
// render frames in between reuse what the first converted.
TEST(ConvertedServerViewTest, ConvertsAnAuthoritativeStateOnlyOnceForEveryRenderFrameItIsRecent) {
  ConvertedServerView converted;
  converted.Update(InMatch(1, 12, {{.entity = EntityId{4}}}));

  converted.Update(InMatch(1, 12, {{.entity = EntityId{4}}, {.entity = EntityId{9}}}));
  ASSERT_EQ(converted.Updates().size(), 1U);
  EXPECT_EQ(converted.Updates()[0].bodies.size(), 1U);

  converted.Update(InMatch(1, {{.tick = 12, .bodies = {{.entity = EntityId{4}}, {.entity = EntityId{9}}}},
                               {.tick = 13, .bodies = {{.entity = EntityId{4}}, {.entity = EntityId{9}}}}}));
  ASSERT_EQ(converted.Updates().size(), 2U);
  EXPECT_EQ(converted.Updates()[0].bodies.size(), 1U);
  EXPECT_EQ(converted.Updates()[1].tick, 13U);
  EXPECT_EQ(converted.Updates()[1].bodies.size(), 2U);
}

TEST(ConvertedServerViewTest, ConvertsTheNextMatchsStateEvenAtATickAlreadySeen) {
  ConvertedServerView converted;
  converted.Update(InMatch(1, 12, {{.entity = EntityId{4}}}));

  converted.Update(InMatch(2, 12, {{.entity = EntityId{5}}}));

  ASSERT_EQ(converted.Updates().size(), 1U);
  ASSERT_EQ(converted.Updates()[0].bodies.size(), 1U);
  EXPECT_EQ(converted.Updates()[0].bodies[0].entity, augusta::presentation::EntityId{5});
}

TEST(ConvertedServerViewTest, IsNoUpdateOnceTheMatchEnds) {
  ConvertedServerView converted;
  augusta::harness::ServerView view = InMatch(1, 12, {{.entity = EntityId{4}}});
  converted.Update(view);

  view.in_match = false;
  view.authoritative.reset();
  view.recent_authoritative.clear();
  view.match_end = augusta::harness::MatchEnd{};
  converted.Update(view);

  EXPECT_TRUE(converted.Updates().empty());
}

TEST(ConvertedServerViewTest, NamesEveryPlayersCharacterInSessionOrder) {
  ConvertedServerView converted;

  converted.Update(InMatch(1, 12, {}));

  const auto characters = converted.Characters();
  ASSERT_EQ(characters.size(), 2U);
  EXPECT_EQ(characters[0].entity, augusta::presentation::EntityId{30});
  EXPECT_EQ(characters[0].character, "sniper");
  EXPECT_EQ(characters[1].entity, augusta::presentation::EntityId{70});
  EXPECT_EQ(characters[1].character, "medic");
}

// Match start is told once per match: render frames reuse its characters until
// the next.
TEST(ConvertedServerViewTest, ConvertsTheCharactersOnlyOnceForEveryMatch) {
  ConvertedServerView converted;
  converted.Update(InMatch(1, 12, {}));

  augusta::harness::ServerView same_match = InMatch(1, 13, {});
  same_match.match_start->players.pop_back();
  converted.Update(same_match);
  EXPECT_EQ(converted.Characters().size(), 2U);

  augusta::harness::ServerView next_match = InMatch(2, 1, {});
  next_match.match_start->players.pop_back();
  converted.Update(next_match);
  ASSERT_EQ(converted.Characters().size(), 1U);
  EXPECT_EQ(converted.Characters()[0].character, "medic");
}

}  // namespace
