#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
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
#include "augusta/weapon.h"
#include "parameters_loader.h"
#include "policy_loader.h"

// SimulationWorld ticked with a Game policy script chosen by the test (ADR-0022,
// ADR-0023): what a hook sees, what it may do and what happens when it fails,
// observed through the TickResult each tick returns and the lines the server logs.
namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::scripting::Engine;
using augusta::scripting::Scripts;
using augusta::simulation::Character;
using augusta::simulation::EntityId;
using augusta::simulation::MatchEnd;
using augusta::simulation::PlayerCommand;
using augusta::simulation::PlayerIdentity;
using augusta::simulation::SessionId;
using augusta::simulation::State;
using augusta::simulation::TickResult;
using augusta::simulation::World;

constexpr std::uint8_t kTickRate = 60;
constexpr float kTick = 1.0F / kTickRate;

constexpr EntityId kAlice = static_cast<EntityId>(1);
constexpr EntityId kBob = static_cast<EntityId>(2);
// Their sessions: numbers of their own, unrelated to their bodies'.
constexpr SessionId kAliceSession = static_cast<SessionId>(11);
constexpr SessionId kBobSession = static_cast<SessionId>(12);
const Character kCharacter{.eye = Vec3(0.0F, 1.5F, 0.0F), .hitboxes = {}};

// The Match end Game policy took on the tick of result, if it took one.
std::optional<MatchEnd> MatchEndOf(const TickResult& result) {
  for (const augusta::simulation::PolicyAction& action : result.actions) {
    if (const auto* end = std::get_if<MatchEnd>(&action)) {
      return *end;
    }
  }
  return std::nullopt;
}

// Alice walking, for every tick.
std::vector<PlayerCommand> Walking() {
  Command command;
  command.movement.direction = Vec3(0.0F, 0.0F, -1.0F);
  return {PlayerCommand{.entity = kAlice, .command = command}};
}

// The engine loaded with objectives as the scenario's objectives.lua.
Engine WithObjectives(std::string objectives) {
  auto engine = Engine::Load(Scripts{.objectives = std::move(objectives), .behaviours = std::nullopt});
  EXPECT_TRUE(engine.has_value()) << augusta::scripting::DescribeLoadError(engine.error());
  return engine ? *std::move(engine) : Engine{};
}

// A world with policy and Alice in it, playing the sniper.
World WithAlice(Engine policy, const Parameters& parameters = Parameters{}) {
  World world(parameters, kTickRate, std::move(policy));
  world.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter,
                  PlayerIdentity{.session = kAliceSession, .character = "characters/sniper"});
  return world;
}

// Where Alice is after walking for ticks in a world with policy.
Vec3 WalkedTo(Engine policy, int ticks) {
  World world = WithAlice(std::move(policy));
  State state;
  for (int i = 0; i < ticks; ++i) {
    state = world.Tick(Walking(), kTick).state;
  }
  return state.bodies.at(0).body.position;
}

// The example scenario's Parameters script, which loads.
std::string ExampleParameters() {
  const std::ifstream file(AUGUSTA_EXAMPLE_PARAMETERS);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

class PolicyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    augusta::logging::Init();
    augusta::logging::SetLogLevel(augusta::logging::Severity::kWarn);
  }

  // Ticks world n times and returns what was logged meanwhile.
  static std::string LogOfTicks(World& world, int ticks) {
    testing::internal::CaptureStdout();
    for (int i = 0; i < ticks; ++i) {
      world.Tick({}, kTick);
    }
    return testing::internal::GetCapturedStdout();
  }
};

TEST_F(PolicyTest, AHookTheObjectivesDefineIsCalledEveryTickWithTheTick) {
  World world = WithAlice(WithObjectives(R"(
    local calls = 0
    function on_tick(match)
      calls = calls + 1
      if match.tick == 5 then
        error("called " .. calls .. " times by tick " .. match.tick)
      end
    end
  )"));

  const std::string log = LogOfTicks(world, 5);

  EXPECT_NE(log.find("called 5 times by tick 5"), std::string::npos) << log;
  EXPECT_NE(log.find("script=objectives.lua hook=on_tick"), std::string::npos) << log;
}

TEST_F(PolicyTest, AHookTheObjectivesDoNotDefineIsANoOp) {
  for (const char* objectives : {"", "local unused = 1", "function some_other_hook() error('not me') end"}) {
    World world = WithAlice(WithObjectives(objectives));

    const std::string log = LogOfTicks(world, 3);

    EXPECT_EQ(log, "") << objectives;
    EXPECT_EQ(WalkedTo(WithObjectives(objectives), 10), WalkedTo(Engine{}, 10)) << objectives;
  }
}

// A hook that fails, and what the log says of it.
struct FailingHook {
  const char* objectives;
  const char* logged;
};

TEST_F(PolicyTest, AFailingHookIsLoggedDecidesNothingAndTheTickGoesOn) {
  for (const FailingHook& hook : {
           FailingHook{.objectives = "function on_tick() error('boom') end", .logged = "boom"},
           FailingHook{.objectives = "function on_tick() while true do end end", .logged = "instruction limit"},
           FailingHook{.objectives = "function on_tick(match) match.tick = 0 end", .logged = "read-only"},
           FailingHook{.objectives = "function on_tick() return function() end end", .logged = "not plain data"},
           FailingHook{.objectives = "function on_tick() return 1, 2 end", .logged = "more than one value"},
           FailingHook{.objectives = "function on_tick() local t = {} t[1] = t return t end",
                       .logged = "not plain data"},
           FailingHook{.objectives = "function on_tick() return {1, x = 2} end", .logged = "not plain data"},
           FailingHook{.objectives = "on_tick = 5", .logged = "not a function"},
           FailingHook{.objectives = "function on_tick() return {winner = 1} end",
                       .logged = "not a player alive in the Match"},
           FailingHook{.objectives = "function on_tick() return {winner = 11.5} end",
                       .logged = "not a player alive in the Match"},
           FailingHook{.objectives = "function on_tick() return {winner = '11'} end",
                       .logged = "neither {winner = <Session ID>} nor {draw = true}"},
           FailingHook{.objectives = "function on_tick() return {draw = false} end",
                       .logged = "neither {winner = <Session ID>} nor {draw = true}"},
           FailingHook{.objectives = "function on_tick() return {winner = 11, draw = true} end",
                       .logged = "neither {winner = <Session ID>} nor {draw = true}"},
           FailingHook{.objectives = "function on_tick() return {winner = 11, by = 'me'} end",
                       .logged = "neither {winner = <Session ID>} nor {draw = true}"},
           FailingHook{.objectives = "function on_tick() return {} end",
                       .logged = "neither {winner = <Session ID>} nor {draw = true}"},
           FailingHook{.objectives = "function on_tick() return true end",
                       .logged = "neither {winner = <Session ID>} nor {draw = true}"},
       }) {
    World world = WithAlice(WithObjectives(hook.objectives));

    testing::internal::CaptureStdout();
    const TickResult first = world.Tick(Walking(), kTick);
    const std::string log = testing::internal::GetCapturedStdout();
    const TickResult second = world.Tick(Walking(), kTick);

    EXPECT_NE(log.find("WARN subsystem=simulationworld"), std::string::npos) << hook.objectives << "\n" << log;
    EXPECT_NE(log.find("script=objectives.lua hook=on_tick tick=1"), std::string::npos) << hook.objectives;
    EXPECT_NE(log.find(hook.logged), std::string::npos) << hook.objectives << "\n" << log;
    EXPECT_TRUE(first.actions.empty()) << hook.objectives;
    EXPECT_TRUE(second.actions.empty()) << hook.objectives;
    EXPECT_EQ(second.state.tick, 2U) << hook.objectives;
    ASSERT_EQ(second.state.bodies.size(), 1U) << hook.objectives;
    EXPECT_EQ(second.state.bodies[0].body.position, WalkedTo(Engine{}, 2)) << hook.objectives;
  }
}

// The instruction limit bounds one call, not the hook's whole life: a hook
// well within it on every call is never stopped, however long the Match.
TEST_F(PolicyTest, TheInstructionLimitStartsOverEveryCall) {
  World world = WithAlice(WithObjectives(R"(
    function on_tick()
      local sum = 0
      for i = 1, 20000 do sum = sum + i end
    end
  )"));

  EXPECT_EQ(LogOfTicks(world, 20), "");
}

// What a hook reaches for that the sandbox leaves out fails the call, as any
// error does.
TEST_F(PolicyTest, TheSandboxRefusesAHookTheMachineAndRandomness) {
  for (const char* call : {"io.open('policy.txt')", "os.time()", "os.execute('echo')", "require('other')",
                           "math.random()", "package.loadlib('x', 'y')", "dofile('other.lua')", "load('return 1')()",
                           "loadfile('other.lua')", "pcall(error)"}) {
    World world = WithAlice(WithObjectives(std::string("function on_tick() ") + call + " end"));

    const std::string log = LogOfTicks(world, 1);

    EXPECT_NE(log.find("event=policy_hook_failed"), std::string::npos) << call << "\n" << log;
  }
}

TEST_F(PolicyTest, AScriptThatUsesWhatTheSandboxLeavesOutAtItsTopLevelDoesNotLoad) {
  for (const char* use :
       {"io.open('policy.txt')", "os.time()", "require('other')", "math.random()", "package.loadlib('x', 'y')"}) {
    for (const bool objectives : {true, false}) {
      Scripts scripts;
      (objectives ? scripts.objectives : scripts.behaviours) = use;

      const auto engine = Engine::Load(scripts);

      ASSERT_FALSE(engine.has_value()) << use;
      EXPECT_NE(
          augusta::scripting::DescribeLoadError(engine.error()).find(objectives ? "objectives.lua" : "behaviours.lua"),
          std::string::npos)
          << use;
    }
  }
}

TEST_F(PolicyTest, AScriptWithASyntaxErrorDoesNotLoad) {
  const auto engine = Engine::Load(Scripts{.objectives = std::nullopt, .behaviours = "function assign_spawns("});

  ASSERT_FALSE(engine.has_value());
  EXPECT_EQ(engine.error().script, augusta::scripting::Script::kBehaviours);
  EXPECT_NE(augusta::scripting::DescribeLoadError(engine.error()).find("behaviours.lua"), std::string::npos);
}

TEST_F(PolicyTest, AScriptWhoseTopLevelNeverFinishesDoesNotLoad) {
  const auto engine = Engine::Load(Scripts{.objectives = "while true do end", .behaviours = std::nullopt});

  ASSERT_FALSE(engine.has_value());
  EXPECT_NE(engine.error().message.find("instruction limit"), std::string::npos) << engine.error().message;
}

// Neither sees the other's globals, whichever is loaded first (ADR-0039).
TEST_F(PolicyTest, TheParametersScriptAndAPolicyScriptShareNoGlobals) {
  auto policy = Engine::Load(Scripts{.objectives = R"(
    policy_global = 1
    function on_tick()
      if parameters_global ~= nil then error("sees the parameters' global") end
    end
  )",
                                     .behaviours = std::nullopt});
  ASSERT_TRUE(policy.has_value());
  const auto parameters = augusta::parameters::Load(
      "if policy_global ~= nil then error('sees the policy global') end\nparameters_global = 1\n" +
      ExampleParameters());
  ASSERT_TRUE(parameters.has_value()) << augusta::parameters::DescribeLoadError(parameters.error());
  World world = WithAlice(*std::move(policy), *parameters);

  EXPECT_EQ(LogOfTicks(world, 1), "");
}

// --- Match start: assign_spawns (US-03) ---

using augusta::simulation::MatchPlayer;

// The engine loaded with behaviours as the scenario's behaviours.lua.
Engine WithBehaviours(std::string behaviours) {
  auto engine = Engine::Load(Scripts{.objectives = std::nullopt, .behaviours = std::move(behaviours)});
  EXPECT_TRUE(engine.has_value()) << augusta::scripting::DescribeLoadError(engine.error());
  return engine ? *std::move(engine) : Engine{};
}

// Three players, sessions 4, 5 and 6 with bodies 11, 12 and 13, playing
// the sniper, the player and the medic.
std::vector<MatchPlayer> ThreePlayers() {
  return {
      MatchPlayer{.entity = static_cast<EntityId>(11),
                  .identity = PlayerIdentity{.session = static_cast<SessionId>(4), .character = "characters/sniper"},
                  .character = kCharacter},
      MatchPlayer{.entity = static_cast<EntityId>(12),
                  .identity = PlayerIdentity{.session = static_cast<SessionId>(5), .character = "characters/player"},
                  .character = kCharacter},
      MatchPlayer{.entity = static_cast<EntityId>(13),
                  .identity = PlayerIdentity{.session = static_cast<SessionId>(6), .character = "characters/medic"},
                  .character = kCharacter},
  };
}

// Three Spawn points far enough apart that no two bodies touch.
const std::vector<Vec3> kSpawnPoints{Vec3(10.0F, 0.0F, 0.0F), Vec3(20.0F, 0.0F, 0.0F), Vec3(30.0F, 0.0F, 0.0F)};

TEST_F(PolicyTest, AnAssignSpawnsAnswerPlacesEachPlayerAtTheSpawnPointItNames) {
  World world(Parameters{}, kTickRate, WithBehaviours(R"(
    function assign_spawns(match)
      return {
        {session = 4, spawn_point = 3},
        {session = 5, spawn_point = 1},
        {session = 6, spawn_point = 2},
      }
    end
  )"));

  const std::vector<Vec3> spawned = world.StartMatch(ThreePlayers(), kSpawnPoints);
  const State first = world.Tick({}, kTick).state;

  EXPECT_EQ(spawned, (std::vector<Vec3>{kSpawnPoints[2], kSpawnPoints[0], kSpawnPoints[1]}));
  ASSERT_EQ(first.bodies.size(), 3U);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_NEAR(first.bodies[i].body.position.x, spawned[i].x, 0.01F) << i;
    EXPECT_NEAR(first.bodies[i].body.position.z, spawned[i].z, 0.01F) << i;
  }
}

// The hook sees each player's Session ID, Entity ID and Character, the
// Player count and the number of Spawn points, and cannot write to them.
TEST_F(PolicyTest, AssignSpawnsIsHandedAReadOnlyViewOfTheMatch) {
  Parameters parameters;
  parameters.player_count = 3;
  World world(parameters, kTickRate, WithBehaviours(R"(
    function assign_spawns(match)
      local seen = string.format("count=%d points=%d", match.player_count, match.spawn_points)
      for _, player in ipairs(match.players) do
        seen = seen .. string.format(" %d/%d/%s", player.session, player.entity, player.character)
      end
      error(seen)
    end
  )"));

  testing::internal::CaptureStdout();
  world.StartMatch(ThreePlayers(), kSpawnPoints);
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_NE(log.find("count=3 points=3 4/11/characters/sniper 5/12/characters/player 6/13/characters/medic"),
            std::string::npos)
      << log;
}

TEST_F(PolicyTest, WithoutAnAssignSpawnsHookPlayersTakeTheSpawnPointsInOrderSilently) {
  for (const char* behaviours : {"", "function assign_spawns() end", "function assign_spawns() return nil end"}) {
    World world(Parameters{}, kTickRate, WithBehaviours(behaviours));

    testing::internal::CaptureStdout();
    const std::vector<Vec3> spawned = world.StartMatch(ThreePlayers(), kSpawnPoints);
    const std::string log = testing::internal::GetCapturedStdout();

    EXPECT_EQ(spawned, kSpawnPoints) << behaviours;
    EXPECT_EQ(log, "") << behaviours;
  }
  World without_policy(Parameters{}, kTickRate);
  EXPECT_EQ(without_policy.StartMatch(ThreePlayers(), kSpawnPoints), kSpawnPoints);
}

// An assign_spawns answer that is refused, and what the log says of it.
struct RefusedAnswer {
  const char* behaviours;
  const char* logged;
};

TEST_F(PolicyTest, ARefusedOrFailingAssignSpawnsAnswerFallsBackToTheInOrderAssignmentAndIsLogged) {
  // Each answer but the one it is about is a valid one.
  for (const RefusedAnswer& answer : {
           RefusedAnswer{.behaviours = "return {{session = 4, spawn_point = 1}, {session = 5, spawn_point = 2}, "
                                       "{session = 6, spawn_point = 4}}",
                         .logged = "spawn_point is not a whole number"},
           RefusedAnswer{.behaviours = "return {{session = 4, spawn_point = 0}, {session = 5, spawn_point = 2}, "
                                       "{session = 6, spawn_point = 3}}",
                         .logged = "spawn_point is not a whole number"},
           RefusedAnswer{.behaviours = "return {{session = 4, spawn_point = 1.5}, {session = 5, spawn_point = 2}, "
                                       "{session = 6, spawn_point = 3}}",
                         .logged = "spawn_point is not a whole number"},
           RefusedAnswer{.behaviours = "return {{session = 4, spawn_point = '1'}, {session = 5, spawn_point = 2}, "
                                       "{session = 6, spawn_point = 3}}",
                         .logged = "spawn_point is not a whole number"},
           RefusedAnswer{.behaviours = "return {{session = 7, spawn_point = 1}, {session = 5, spawn_point = 2}, "
                                       "{session = 6, spawn_point = 3}}",
                         .logged = "not a player in the Match"},
           RefusedAnswer{.behaviours = "return {{session = 4, spawn_point = 1}, {session = 4, spawn_point = 2}, "
                                       "{session = 6, spawn_point = 3}}",
                         .logged = "names a player twice"},
           RefusedAnswer{.behaviours = "return {{session = 4, spawn_point = 1}, {session = 6, spawn_point = 3}}",
                         .logged = "leaves a player out"},
           RefusedAnswer{.behaviours = "return {{session = 4, spawn_point = 1, team = 1}, {session = 5, spawn_point = "
                                       "2}, {session = 6, spawn_point = 3}}",
                         .logged = "not a record of exactly a session and a spawn_point"},
           RefusedAnswer{.behaviours = "return {3, 1, 2}", .logged = "not a record of exactly"},
           RefusedAnswer{.behaviours = "return {session = 4, spawn_point = 1}", .logged = "not a list"},
           RefusedAnswer{.behaviours = "return 3", .logged = "not a list"},
           RefusedAnswer{.behaviours = "error('boom')", .logged = "boom"},
           RefusedAnswer{.behaviours = "while true do end", .logged = "instruction limit"},
           RefusedAnswer{.behaviours = "return function() end", .logged = "not plain data"},
       }) {
    World world(Parameters{}, kTickRate,
                WithBehaviours(std::string("function assign_spawns(match) ") + answer.behaviours + " end"));

    testing::internal::CaptureStdout();
    const std::vector<Vec3> spawned = world.StartMatch(ThreePlayers(), kSpawnPoints);
    const std::string log = testing::internal::GetCapturedStdout();

    EXPECT_EQ(spawned, kSpawnPoints) << answer.behaviours;
    EXPECT_NE(log.find("WARN subsystem=simulationworld"), std::string::npos) << answer.behaviours << "\n" << log;
    EXPECT_NE(log.find("script=behaviours.lua hook=assign_spawns"), std::string::npos) << answer.behaviours;
    EXPECT_NE(log.find(answer.logged), std::string::npos) << answer.behaviours << "\n" << log;
  }
}

TEST_F(PolicyTest, MorePlayersThanSpawnPointsStartOverInOrderAndNoSpawnPointsSpawnAtTheOrigin) {
  World world(Parameters{}, kTickRate);

  EXPECT_EQ(world.StartMatch(ThreePlayers(), {kSpawnPoints[0], kSpawnPoints[1]}),
            (std::vector<Vec3>{kSpawnPoints[0], kSpawnPoints[1], kSpawnPoints[0]}));
  EXPECT_EQ(world.StartMatch(ThreePlayers(), {}), (std::vector<Vec3>{Vec3{}, Vec3{}, Vec3{}}));
}

// Nothing carries over: a Match starts from fresh bodies, health and rifles,
// and no player of the last one is left in the world.
TEST_F(PolicyTest, EveryMatchStartsFromFreshBodiesAtFullHealthWithFullRifles) {
  Parameters parameters;
  parameters.rifle.magazine_capacity = 30;
  parameters.rifle.rounds_per_minute = 600.0F;
  parameters.starting_health = 100.0F;
  World world(parameters, kTickRate);
  world.StartMatch(ThreePlayers(), kSpawnPoints);
  Command firing;
  firing.fire = true;
  firing.movement.direction = Vec3(0.0F, 0.0F, -1.0F);
  for (int i = 0; i < 30; ++i) {
    world.Tick({PlayerCommand{.entity = static_cast<EntityId>(11), .command = firing}}, kTick);
  }

  std::vector<MatchPlayer> next = ThreePlayers();
  for (MatchPlayer& player : next) {
    player.entity = static_cast<EntityId>(std::to_underlying(player.entity) + 10);
  }
  world.StartMatch(next, kSpawnPoints);
  const State first = world.Tick({}, kTick).state;

  ASSERT_EQ(first.bodies.size(), 3U);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(first.bodies[i].entity, next[i].entity);
    EXPECT_EQ(first.bodies[i].health, 100.0F);
    EXPECT_EQ(first.bodies[i].rifle, augusta::weapon::Loaded(parameters.rifle));
    EXPECT_NEAR(first.bodies[i].body.position.z, kSpawnPoints[i].z, 0.01F);
  }
}

// --- The example scenario's policy scripts, as the server loads them (ADR-0013) ---

// The example scenario's Game policy, read out of its golden server pack by
// the server's own loader.
Engine ExamplePolicy() {
  const std::filesystem::path packs{AUGUSTA_EXAMPLE_PACKS};
  const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(packs / "test.pub");
  EXPECT_TRUE(public_key.has_value());
  auto pack = augusta::assets::Pack::Load(packs / "server.pack", public_key.value());
  EXPECT_TRUE(pack.has_value()) << augusta::assets::DescribeLoadError(pack.error());
  auto policy = augusta::server::LoadPolicy(pack.value());
  EXPECT_TRUE(policy.has_value()) << augusta::server::DescribePolicyLoadError(policy.error());
  return policy ? *std::move(policy) : Engine{};
}

// count players, sessions 1 to count with bodies 101 on, all playing the player.
std::vector<MatchPlayer> Players(std::size_t count) {
  std::vector<MatchPlayer> players;
  for (std::size_t i = 1; i <= count; ++i) {
    players.push_back(
        MatchPlayer{.entity = static_cast<EntityId>(100 + i),
                    .identity = PlayerIdentity{.session = static_cast<SessionId>(i), .character = "characters/player"},
                    .character = kCharacter});
  }
  return players;
}

// count Spawn points a few meters apart down the X axis.
std::vector<Vec3> SpawnPointsInALine(std::size_t count) {
  std::vector<Vec3> points;
  for (std::size_t i = 0; i < count; ++i) {
    points.emplace_back(5.0F * static_cast<float>(i), 0.0F, 0.0F);
  }
  return points;
}

TEST_F(PolicyTest, TheExampleBehavioursGiveEveryPlayerADistinctSpawnPointForEveryPlayerCount) {
  for (std::size_t count = 1; count <= 8; ++count) {
    World world(Parameters{}, kTickRate, ExamplePolicy());

    testing::internal::CaptureStdout();
    std::vector<Vec3> spawned = world.StartMatch(Players(count), SpawnPointsInALine(8));
    const std::string log = testing::internal::GetCapturedStdout();

    EXPECT_EQ(log, "") << count << " players";
    ASSERT_EQ(spawned.size(), count);
    std::ranges::sort(spawned, {}, &Vec3::x);
    EXPECT_EQ(std::ranges::adjacent_find(spawned), spawned.end()) << count << " players";
  }
}

TEST_F(PolicyTest, TheExampleBehavioursStartTheSpawnPointsOverOnlyWhenTheMapHasFewerThanPlayers) {
  World world(Parameters{}, kTickRate, ExamplePolicy());

  const std::vector<Vec3> points = SpawnPointsInALine(3);
  const std::vector<Vec3> spawned = world.StartMatch(Players(5), points);

  EXPECT_EQ(spawned, (std::vector<Vec3>{points[0], points[1], points[2], points[0], points[1]}));
}

// What the objectives' on_tick hook may decide (ADR-0022): to end the Match with
// a winner, or as a draw. A decision is in the State of the tick that made it,
// for Host to act on after it (ADR-0023).
TEST_F(PolicyTest, AWinnerOnTickDeclaresIsAnActionOfTheTickThatDeclaredIt) {
  World world = WithAlice(WithObjectives(R"(
    function on_tick(match)
      if match.tick == 3 then return {winner = 11} end
    end
  )"));

  const TickResult first = world.Tick({}, kTick);
  const TickResult second = world.Tick({}, kTick);
  const TickResult third = world.Tick({}, kTick);

  EXPECT_TRUE(first.actions.empty());
  EXPECT_TRUE(second.actions.empty());
  ASSERT_EQ(third.actions.size(), 1U);
  ASSERT_TRUE(MatchEndOf(third).has_value());
  EXPECT_EQ(MatchEndOf(third)->winner, kAliceSession);
}

TEST_F(PolicyTest, ADrawOnTickDeclaresIsAMatchEndWithNoWinner) {
  World world = WithAlice(WithObjectives("function on_tick() return {draw = true} end"));

  const TickResult result = world.Tick({}, kTick);

  ASSERT_TRUE(MatchEndOf(result).has_value());
  EXPECT_FALSE(MatchEndOf(result)->winner.has_value());
}

// Policy ends a Match once: once it has decided, on_tick is not asked again
// until the Match is over, and in the next Match it is asked afresh.
TEST_F(PolicyTest, OnceOnTickHasEndedTheMatchItIsNotCalledAgainUntilTheNextMatch) {
  World world = WithAlice(WithObjectives(R"(
    function on_tick(match) return {winner = match.players[1].session} end
  )"));
  ASSERT_TRUE(MatchEndOf(world.Tick({}, kTick)).has_value());

  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(world.Tick({}, kTick).actions.empty()) << "tick " << i;
  }

  world.EndMatch();
  world.AddPlayer(kBob, Vec3(0.0F, 0.0F, 0.0F), kCharacter,
                  PlayerIdentity{.session = kBobSession, .character = "characters/player"});
  const TickResult next = world.Tick({}, kTick);
  ASSERT_TRUE(MatchEndOf(next).has_value());
  EXPECT_EQ(MatchEndOf(next)->winner, kBobSession);
}

// A world with no one in it has no Match for policy to decide on: the Lobby.
TEST_F(PolicyTest, OnTickIsNotCalledWhileTheWorldHasNoPlayers) {
  World world(Parameters{}, kTickRate, WithObjectives("function on_tick() error('called in the lobby') end"));

  EXPECT_EQ(LogOfTicks(world, 3), "");
}

TEST_F(PolicyTest, OnTickSeesTheTickThePlayerCountAndEveryPlayerInTheMatch) {
  Parameters parameters;
  parameters.player_count = 2;
  parameters.starting_health = 75.0F;
  World world = WithAlice(WithObjectives(R"(
    function on_tick(match)
      local seen = "tick=" .. match.tick .. " player_count=" .. match.player_count
      for _, player in ipairs(match.players) do
        seen = seen .. " [session=" .. player.session .. " entity=" .. player.entity ..
          " character=" .. player.character .. " alive=" .. tostring(player.alive) ..
          " health=" .. player.health .. " killed=" .. tostring(player.killed) ..
          " killer=" .. tostring(player.killer) .. "]"
      end
      error(seen)
    end
  )"),
                          parameters);
  world.AddPlayer(kBob, Vec3(5.0F, 0.0F, 0.0F), kCharacter,
                  PlayerIdentity{.session = kBobSession, .character = "characters/player"});

  const std::string log = LogOfTicks(world, 1);

  EXPECT_NE(log.find("tick=1 player_count=2"
                     " [session=11 entity=1 character=characters/sniper alive=true health=75 killed=false"
                     " killer=nil]"
                     " [session=12 entity=2 character=characters/player alive=true health=75 killed=false"
                     " killer=nil]"),
            std::string::npos)
      << log;
}

TEST_F(PolicyTest, APlayerWhoLeftTheMatchIsAbsentFromWhatOnTickSees) {
  World world = WithAlice(WithObjectives(R"(
    function on_tick(match)
      if #match.players == 1 then return {winner = match.players[1].session} end
    end
  )"));
  world.AddPlayer(kBob, Vec3(5.0F, 0.0F, 0.0F), kCharacter,
                  PlayerIdentity{.session = kBobSession, .character = "characters/player"});
  ASSERT_FALSE(MatchEndOf(world.Tick({}, kTick)).has_value());

  world.RemovePlayer(kAlice);
  const TickResult result = world.Tick({}, kTick);

  ASSERT_TRUE(MatchEndOf(result).has_value());
  EXPECT_EQ(MatchEndOf(result)->winner, kBobSession);
}

// A body added with no identity has Session ID 0, which is no player's: it is
// never declared the winner, whatever policy says.
TEST_F(PolicyTest, SessionZeroIsNeverTheWinner) {
  World world(Parameters{}, kTickRate, WithObjectives("function on_tick() return {winner = 0} end"));
  world.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);

  EXPECT_FALSE(MatchEndOf(world.Tick({}, kTick)).has_value());
}

TEST_F(PolicyTest, AWinnerWhoIsNotInTheMatchIsRefusedLoggedAndTheMatchGoesOn) {
  World world = WithAlice(WithObjectives("function on_tick() return {winner = 12} end"));

  testing::internal::CaptureStdout();
  const TickResult result = world.Tick({}, kTick);
  const std::string log = testing::internal::GetCapturedStdout();

  EXPECT_TRUE(result.actions.empty());
  EXPECT_NE(log.find("event=policy_decision_refused script=objectives.lua hook=on_tick tick=1"), std::string::npos)
      << log;
}

// A box 2 m tall and 4 m wide across a player's feet, facing along Z: hard to
// miss from in front or behind.
Character Broad() {
  using augusta::ballistics::Triangle;
  const Vec3 up(0.0F, 2.0F, 0.0F);
  const Vec3 half(2.0F, 0.0F, 0.0F);
  return Character{.eye = Vec3(0.0F, 1.5F, 0.0F),
                   .hitboxes = {{.part = augusta::ballistics::BodyPart::kTorso,
                                 .triangles = {Triangle{.a = -half, .b = half, .c = half + up},
                                               Triangle{.a = -half, .b = half + up, .c = -half + up}}}}};
}

augusta::physics::CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return augusta::physics::CollisionMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                                                    Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                                         .indices = {0, 1, 2, 0, 2, 3}};
}

// Alice, at the origin, and Bob, 10 m down -Z where her level view of yaw 0
// looks, on a floor, with rounds that kill at once; on_tick is objectives.
World Duel(std::string objectives) {
  Parameters parameters;
  parameters.player_count = 2;
  parameters.rifle.rounds_per_minute = 600.0F;
  parameters.rifle.magazine_capacity = 30;
  parameters.rifle.muzzle_velocity = 800.0F;
  parameters.ammo.max_range = 200.0F;
  parameters.ammo.damage = {.head = 100.0F, .torso = 100.0F, .limb = 100.0F};
  parameters.starting_health = 100.0F;
  World world(parameters, kTickRate, WithObjectives(std::move(objectives)));
  EXPECT_TRUE(world.AddCollisionMesh(Floor()).has_value());
  world.AddPlayer(kAlice, Vec3(0.0F, 0.5F, 0.0F), Broad(),
                  PlayerIdentity{.session = kAliceSession, .character = "characters/player"});
  world.AddPlayer(kBob, Vec3(0.0F, 0.5F, -10.0F), Broad(),
                  PlayerIdentity{.session = kBobSession, .character = "characters/player"});
  for (int i = 0; i < 30; ++i) {
    world.Tick({}, kTick);
  }
  return world;
}

// The tick on which Alice fires her one round, which kills Bob.
TickResult AliceKillsBob(World& world) {
  Command fire;
  fire.fire = true;
  return world.Tick({PlayerCommand{.entity = kAlice, .command = fire}}, kTick);
}

// On the tick Bob dies the hook reports what it sees of him; on the next, with
// Bob dead but no longer killed on that tick, it ends the Match.
TEST_F(PolicyTest, OnTickSeesWhoWasKilledThisTickAndByWhomAndThatTheyAreDead) {
  World world = Duel(R"(
    function on_tick(match)
      local bob = match.players[2]
      if bob.killed then
        error("health=" .. bob.health .. " alive=" .. tostring(bob.alive) .. " killer=" .. bob.killer)
      end
      if not bob.alive and bob.killer == nil then return {draw = true} end
    end
  )");

  testing::internal::CaptureStdout();
  const TickResult killing = AliceKillsBob(world);
  const std::string log = testing::internal::GetCapturedStdout();
  const TickResult next = world.Tick({}, kTick);

  ASSERT_EQ(killing.state.deaths.size(), 1U);
  EXPECT_NE(log.find("health=0 alive=false killer=1"), std::string::npos) << log;
  EXPECT_FALSE(MatchEndOf(killing).has_value());
  EXPECT_TRUE(MatchEndOf(next).has_value());
}

TEST_F(PolicyTest, ADeadPlayerCannotBeDeclaredTheWinner) {
  World world = Duel(R"(
    function on_tick(match)
      if not match.players[2].alive then return {winner = match.players[2].session} end
    end
  )");

  const TickResult killing = AliceKillsBob(world);

  ASSERT_EQ(killing.state.deaths.size(), 1U);
  EXPECT_FALSE(MatchEndOf(killing).has_value());
}

// Ending a Match leaves nothing of it for the next: no body and no bullet in flight.
TEST_F(PolicyTest, EndingTheMatchTakesEveryPlayerAndEveryBulletInFlightOutOfTheWorld) {
  Parameters parameters;
  parameters.rifle.muzzle_velocity = 100.0F;
  parameters.ammo.max_range = 1000.0F;
  World world = WithAlice(Engine{}, parameters);
  world.AddPlayer(kBob, Vec3(5.0F, 0.0F, 0.0F), kCharacter,
                  PlayerIdentity{.session = kBobSession, .character = "characters/player"});
  Command fire;
  fire.fire = true;
  fire.pitch = 0.5F;
  ASSERT_EQ(world.Tick({PlayerCommand{.entity = kAlice, .command = fire}}, kTick).state.bullets_in_flight, 1U);

  world.EndMatch();
  const State state = world.Tick({}, kTick).state;

  EXPECT_TRUE(state.bodies.empty());
  EXPECT_TRUE(state.alive.empty());
  EXPECT_EQ(state.bullets_in_flight, 0U);
  EXPECT_TRUE(state.map_impacts.empty());
}

}  // namespace
