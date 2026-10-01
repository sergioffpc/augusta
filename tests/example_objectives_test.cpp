#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "encoder.h"
#include "policy_loader.h"

// The example scenario's shipped objectives.lua, run inside the real engine
// through SimulationWorld (ADR-0013's decision for Lua gameplay scripts): loaded
// out of a signed pack as the server loads it, and checked for last player
// standing (US-14) with every Player count from 1 to 8.
namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::simulation::Character;
using augusta::simulation::EntityId;
using augusta::simulation::PlayerCommand;
using augusta::simulation::PlayerIdentity;
using augusta::simulation::SessionId;
using augusta::simulation::State;
using augusta::simulation::World;

constexpr std::uint8_t kTickRate = 60;
constexpr float kTick = 1.0F / kTickRate;
constexpr float kPi = std::numbers::pi_v<float>;
// How far from the shooter, at the arena's centre, every other player stands.
constexpr float kRadius = 8.0F;
// Long enough for a body to settle on the floor, and for a rifle to be ready to fire again.
constexpr int kSettleTicks = 30;
constexpr int kReadyTicks = 8;

std::string ReadFile(const std::filesystem::path& path) {
  const std::ifstream file(path);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

augusta::assets::AssetEntry ScriptEntry(std::string path, const std::string& text) {
  return augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kScript,
                                     .path = std::move(path),
                                     .data = augusta::assets::EncodeScriptBlob(text).value()};
}

// The example scenario's Game policy, written into a signed server pack and
// loaded back out of it by the server's own loader. ctest runs every test in a
// process of its own, possibly in parallel, so each writes a pack of its own.
augusta::scripting::Engine ExamplePolicy() {
  const std::filesystem::path scenario{AUGUSTA_EXAMPLE_SCENARIO};
  const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
  std::string name = std::string("augusta_example_objectives_test_") + test.test_suite_name() + "_" + test.name();
  std::ranges::replace(name, '/', '_');
  const auto pack_path = std::filesystem::temp_directory_path() / (name + ".pack");
  const auto keys = augusta::assets::GenerateEd25519KeyPair();
  EXPECT_TRUE(augusta::assets::WritePack(pack_path,
                                         {ScriptEntry("objectives.lua", ReadFile(scenario / "objectives.lua")),
                                          ScriptEntry("behaviours.lua", ReadFile(scenario / "behaviours.lua"))},
                                         keys.private_key)
                  .has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  std::filesystem::remove(pack_path);
  if (!pack.has_value()) {
    ADD_FAILURE() << augusta::assets::DescribeLoadError(pack.error());
    return augusta::scripting::Engine{};
  }
  auto policy = augusta::server::LoadPolicy(*pack);
  EXPECT_TRUE(policy.has_value()) << augusta::server::DescribePolicyLoadError(policy.error());
  return policy ? *std::move(policy) : augusta::scripting::Engine{};
}

// A target hard to miss from any side: two walls 4 m wide and 2 m tall,
// crossing at its feet.
Character Cross() {
  using augusta::ballistics::Triangle;
  const Vec3 up(0.0F, 2.0F, 0.0F);
  std::vector<Triangle> triangles;
  for (const Vec3& half : {Vec3(2.0F, 0.0F, 0.0F), Vec3(0.0F, 0.0F, 2.0F)}) {
    triangles.push_back({.a = -half, .b = half, .c = half + up});
    triangles.push_back({.a = -half, .b = half + up, .c = -half + up});
  }
  return Character{.eye = Vec3(0.0F, 1.5F, 0.0F),
                   .hitboxes = {{.part = augusta::ballistics::BodyPart::kTorso, .triangles = std::move(triangles)}}};
}

augusta::physics::CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return augusta::physics::CollisionMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                                                    Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                                         .indices = {0, 1, 2, 0, 2, 3}};
}

// A Match of player_count players under the example's objectives, on a floor,
// with rounds that kill at once. Player 0, the shooter, stands at the centre;
// every other stands kRadius away, each in a direction of its own, so the
// shooter can turn to each and shoot it, and each can shoot the shooter back.
// Player i's body is entity i + 1 and its session 100 + i.
class Arena {
 public:
  explicit Arena(std::size_t player_count) : world_(WithLethalRifle(player_count), kTickRate, ExamplePolicy()) {
    EXPECT_TRUE(world_.AddCollisionMesh(Floor()).has_value());
    for (std::size_t i = 0; i < player_count; ++i) {
      Add(i, i == 0 ? Vec3(0.0F, 0.5F, 0.0F) : Vec3(0.0F, 0.5F, 0.0F) + (kRadius * Direction(i)));
    }
    Wait(kSettleTicks);
  }

  static EntityId Entity(std::size_t player) { return static_cast<EntityId>(player + 1); }
  static SessionId Session(std::size_t player) { return static_cast<SessionId>(100 + player); }

  // Puts player at position: one who is not in the Match yet, for a test that
  // brings one in.
  void Add(std::size_t player, const Vec3& position) {
    world_.AddPlayer(Entity(player), position, Cross(), PlayerIdentity{.session = Session(player), .character = 1});
  }

  void Leave(std::size_t player) { world_.RemovePlayer(Entity(player)); }

  // One tick on which each of the players in fire fires once, the shooter at
  // the other it names and every other at the shooter. Returns its State.
  State Fire(const std::vector<std::pair<std::size_t, std::size_t>>& fire) {
    std::vector<PlayerCommand> commands;
    for (const auto& [shooter, target] : fire) {
      Command command{};
      command.fire = true;
      // Yaw 0 looks down -Z, and a yaw turns the view counter-clockwise seen from above.
      command.yaw = shooter == 0 ? Yaw(target) : Wrapped(Yaw(shooter) + kPi);
      commands.push_back(PlayerCommand{.entity = Entity(shooter), .command = command});
    }
    return Record(world_.Tick(commands, kTick));
  }

  // Ticks with nobody doing anything; returns the last State.
  State Wait(int ticks) {
    State state;
    for (int i = 0; i < ticks; ++i) {
      state = Record(world_.Tick({}, kTick));
    }
    return state;
  }

  // The shooter kills player, then waits for its rifle; returns the State of the killing tick.
  State Kill(std::size_t player) {
    const State killing = Fire({{0, player}});
    EXPECT_EQ(killing.deaths.size(), 1U) << "player " << player;
    Wait(kReadyTicks);
    return killing;
  }

  // Every Match end the objectives have decided so far, with the tick of each.
  const std::vector<std::pair<augusta::tick::Tick, augusta::simulation::MatchEnd>>& Ends() const { return ends_; }

 private:
  static Parameters WithLethalRifle(std::size_t player_count) {
    Parameters parameters;
    parameters.player_count = static_cast<std::uint8_t>(player_count);
    parameters.rifle.rounds_per_minute = 600.0F;
    parameters.rifle.magazine_capacity = 30;
    parameters.rifle.muzzle_velocity = 800.0F;
    parameters.ammo.max_range = 200.0F;
    parameters.ammo.damage = {.head = 100.0F, .torso = 100.0F, .limb = 100.0F};
    parameters.starting_health = 100.0F;
    return parameters;
  }

  // angle, as the same turn within -pi to pi: a view's yaw travels on a grid
  // that reaches no further than 4 rad either way (ADR-0038).
  static float Wrapped(float angle) { return std::remainder(angle, 2.0F * kPi); }

  // The yaw of the direction from the centre to player, one of 7 around it.
  static float Yaw(std::size_t player) { return Wrapped(2.0F * kPi * static_cast<float>(player - 1) / 7.0F); }

  static Vec3 Direction(std::size_t player) { return Vec3(-std::sin(Yaw(player)), 0.0F, -std::cos(Yaw(player))); }

  State Record(State state) {
    if (state.match_end.has_value()) {
      ends_.emplace_back(state.tick, *state.match_end);
    }
    return state;
  }

  World world_;
  std::vector<std::pair<augusta::tick::Tick, augusta::simulation::MatchEnd>> ends_;
};

class ExampleObjectivesTest : public ::testing::TestWithParam<std::size_t> {
 protected:
  void SetUp() override {
    augusta::logging::Init();
    augusta::logging::SetLogLevel(augusta::logging::Severity::kWarn);
  }
};

// With two or more players: the Match ends on the tick only one is left alive,
// and that one wins.
class LastPlayerStandingTest : public ExampleObjectivesTest {};

TEST_P(LastPlayerStandingTest, TheLastPlayerAliveWinsOnTheTickTheOthersAreAllDead) {
  const std::size_t players = GetParam();
  Arena arena(players);

  for (std::size_t victim = 1; victim + 1 < players; ++victim) {
    arena.Kill(victim);
    ASSERT_TRUE(arena.Ends().empty()) << "ended with " << players - victim << " of " << players << " alive";
  }
  const State last = arena.Kill(players - 1);

  ASSERT_EQ(arena.Ends().size(), 1U);
  EXPECT_EQ(arena.Ends()[0].first, last.tick);
  EXPECT_EQ(arena.Ends()[0].second.winner, Arena::Session(0));
}

TEST_P(LastPlayerStandingTest, TheLastTwoDyingOnTheSameTickIsADraw) {
  const std::size_t players = GetParam();
  Arena arena(players);
  for (std::size_t victim = 1; victim + 1 < players; ++victim) {
    arena.Kill(victim);
  }

  const State last = arena.Fire({{0, players - 1}, {players - 1, 0}});

  ASSERT_EQ(last.deaths.size(), 2U);
  ASSERT_EQ(arena.Ends().size(), 1U);
  EXPECT_EQ(arena.Ends()[0].first, last.tick);
  EXPECT_FALSE(arena.Ends()[0].second.winner.has_value());
}

// The players who leave are out of the Match for the win condition (US-20):
// the one left wins, by the same rule.
TEST_P(LastPlayerStandingTest, WhenAllButOnePlayerLeaveTheOneLeftWins) {
  const std::size_t players = GetParam();
  Arena arena(players);

  for (std::size_t player = 0; player + 1 < players; ++player) {
    arena.Leave(player);
  }
  const State next = arena.Wait(1);

  ASSERT_EQ(arena.Ends().size(), 1U);
  EXPECT_EQ(arena.Ends()[0].first, next.tick);
  EXPECT_EQ(arena.Ends()[0].second.winner, Arena::Session(players - 1));
}

// A Match everyone has left has no one for policy to decide on: the server
// ends it on its own, as it always has.
TEST_P(LastPlayerStandingTest, AMatchEveryoneHasLeftIsNotEndedByTheObjectives) {
  const std::size_t players = GetParam();
  Arena arena(players);

  for (std::size_t player = 0; player < players; ++player) {
    arena.Leave(player);
  }
  arena.Wait(kSettleTicks);

  EXPECT_TRUE(arena.Ends().empty());
}

TEST_P(LastPlayerStandingTest, WhileTwoOrMoreAreAliveTheMatchGoesOn) {
  const std::size_t players = GetParam();
  Arena arena(players);
  if (players > 2) {
    arena.Kill(1);
  }

  arena.Wait(10 * kTickRate);

  EXPECT_TRUE(arena.Ends().empty());
}

INSTANTIATE_TEST_SUITE_P(PlayerCounts, LastPlayerStandingTest, ::testing::Range<std::size_t>(2, 9));

// A Match that started with one player (ADR-0043's development Match) is a
// draw once that player dies, and goes on while it lives.
TEST_F(ExampleObjectivesTest, ASoloMatchGoesOnWhileItsPlayerLivesAndIsADrawWhenItDies) {
  Arena arena(1);
  arena.Wait(10 * kTickRate);
  ASSERT_TRUE(arena.Ends().empty());

  // In v1 only rounds kill, and a round never hits its shooter: the round that
  // kills the solo player is fired by another player, 30 m off, who leaves
  // while it is in flight.
  arena.Add(1, Vec3(0.0F, 0.5F, -30.0F));
  arena.Wait(kSettleTicks);
  arena.Fire({{1, 0}});
  arena.Leave(1);
  arena.Wait(kReadyTicks);

  ASSERT_EQ(arena.Ends().size(), 1U);
  EXPECT_FALSE(arena.Ends()[0].second.winner.has_value());
}

}  // namespace
