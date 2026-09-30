#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "parameters_loader.h"

// SimulationWorld ticked with a Game policy script chosen by the test (ADR-0022,
// ADR-0023): what a hook sees, what it may do and what happens when it fails,
// observed through the State each tick returns and the lines the server logs.
namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::scripting::Engine;
using augusta::scripting::Scripts;
using augusta::simulation::Character;
using augusta::simulation::EntityId;
using augusta::simulation::PlayerCommand;
using augusta::simulation::State;
using augusta::simulation::World;

constexpr std::uint8_t kTickRate = 60;
constexpr float kTick = 1.0F / kTickRate;

constexpr EntityId kAlice = static_cast<EntityId>(1);
const Character kCharacter{.eye = Vec3(0.0F, 1.5F, 0.0F), .hitboxes = {}};

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

// Where Alice is after walking for ticks in a world with policy.
Vec3 WalkedTo(Engine policy, int ticks) {
  World world(Parameters{}, kTickRate, std::move(policy));
  world.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);
  State state;
  for (int i = 0; i < ticks; ++i) {
    state = world.Tick(Walking(), kTick);
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
  World world(Parameters{}, kTickRate, WithObjectives(R"(
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
    World world(Parameters{}, kTickRate, WithObjectives(objectives));

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
           FailingHook{.objectives = "function on_tick() return {winner = 1} end", .logged = "decides nothing"},
       }) {
    World world(Parameters{}, kTickRate, WithObjectives(hook.objectives));
    world.AddPlayer(kAlice, Vec3(0.0F, 0.0F, 0.0F), kCharacter);

    testing::internal::CaptureStdout();
    world.Tick(Walking(), kTick);
    const std::string log = testing::internal::GetCapturedStdout();
    const State second = world.Tick(Walking(), kTick);

    EXPECT_NE(log.find("WARN subsystem=simulationworld"), std::string::npos) << hook.objectives << "\n" << log;
    EXPECT_NE(log.find("script=objectives.lua hook=on_tick tick=1"), std::string::npos) << hook.objectives;
    EXPECT_NE(log.find(hook.logged), std::string::npos) << hook.objectives << "\n" << log;
    EXPECT_EQ(second.tick, 2U) << hook.objectives;
    ASSERT_EQ(second.bodies.size(), 1U) << hook.objectives;
    EXPECT_EQ(second.bodies[0].body.position, WalkedTo(Engine{}, 2)) << hook.objectives;
  }
}

// The instruction limit bounds one call, not the hook's whole life: a hook
// well within it on every call is never stopped, however long the Match.
TEST_F(PolicyTest, TheInstructionLimitStartsOverEveryCall) {
  World world(Parameters{}, kTickRate, WithObjectives(R"(
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
    World world(Parameters{}, kTickRate, WithObjectives(std::string("function on_tick() ") + call + " end"));

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
  World world(*parameters, kTickRate, *std::move(policy));

  EXPECT_EQ(LogOfTicks(world, 1), "");
}

}  // namespace
