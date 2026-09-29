#include "parameters_loader.h"

#include <expected>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

// The loader is a pure function of the script text (ADR-0039): every case here
// is a script in, Parameters or an error out.
namespace {

using augusta::parameters::DescribeLoadError;
using augusta::parameters::Load;
using augusta::parameters::LoadErrorCode;

// The rifle, ammo and health entries every complete script below holds.
constexpr std::string_view kCombat = R"(
  rifle = {
    magazine_capacity = 30,
    rounds_per_minute = 600,
    muzzle_velocity = 800,
    reload_seconds = 2.5,
    recoil_pattern = { { pitch = 0.01, yaw = 0.002 }, { pitch = 0.008, yaw = -0.002 } },
    recoil_recovery_per_second = 0.2,
    ads_recoil_scale = 0.5,
    ads_field_of_view = 0.7,
  },
  ammo = {
    gravity = 9.81,
    max_range = 1000,
    damage = { head = 100, torso = 34, limb = 25 },
  },
  starting_health = 100,
)";

// A complete script: fields (the player count and stamina) and the combat entries.
std::string Complete(std::string_view fields) {
  return "return { " + std::string(fields) + ", " + std::string(kCombat) + " }";
}

const std::string kValid = Complete(
    "player_count = 4, stamina = { deplete_per_second = 0.2, regen_per_second = 0.1, forced_walk_below = 0.05 }");

// kValid with its one occurrence of from replaced by to.
std::string Replacing(std::string_view from, std::string_view to) {
  std::string script = kValid;
  const std::size_t at = script.find(from);
  EXPECT_NE(at, std::string::npos) << from;
  if (at != std::string::npos) {
    script.replace(at, from.size(), to);
  }
  return script;
}

// A complete script whose player count is the given Lua expression.
std::string WithPlayerCount(std::string_view player_count) {
  return Complete("player_count = " + std::string(player_count) +
                  ", stamina = { deplete_per_second = 0, regen_per_second = 0, forced_walk_below = 0 }");
}

// A complete script whose three stamina values are the given Lua expressions.
std::string WithStamina(std::string_view deplete, std::string_view regen, std::string_view forced_walk_below) {
  return Complete("player_count = 1, stamina = { deplete_per_second = " + std::string(deplete) +
                  ", regen_per_second = " + std::string(regen) +
                  ", forced_walk_below = " + std::string(forced_walk_below) + " }");
}

void ExpectError(std::string_view script, LoadErrorCode code, std::string_view subject) {
  const auto loaded = Load(script);
  ASSERT_FALSE(loaded.has_value()) << script;
  EXPECT_EQ(loaded.error().code, code) << script;
  EXPECT_EQ(loaded.error().subject, subject) << script;
}

TEST(ParametersLoaderTest, ReadsEveryValueOfACompleteScript) {
  const auto loaded = Load(kValid);

  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->player_count, 4U);
  EXPECT_FLOAT_EQ(loaded->stamina.deplete_per_second, 0.2F);
  EXPECT_FLOAT_EQ(loaded->stamina.regen_per_second, 0.1F);
  EXPECT_FLOAT_EQ(loaded->stamina.forced_walk_below, 0.05F);
  const augusta::parameters::Rifle& rifle = loaded->rifle;
  EXPECT_EQ(rifle.magazine_capacity, 30U);
  EXPECT_FLOAT_EQ(rifle.rounds_per_minute, 600.0F);
  EXPECT_FLOAT_EQ(rifle.muzzle_velocity, 800.0F);
  EXPECT_FLOAT_EQ(rifle.reload_seconds, 2.5F);
  ASSERT_EQ(rifle.recoil_pattern.size(), 2U);
  EXPECT_FLOAT_EQ(rifle.recoil_pattern[0].pitch, 0.01F);
  EXPECT_FLOAT_EQ(rifle.recoil_pattern[0].yaw, 0.002F);
  EXPECT_FLOAT_EQ(rifle.recoil_pattern[1].pitch, 0.008F);
  EXPECT_FLOAT_EQ(rifle.recoil_pattern[1].yaw, -0.002F);
  EXPECT_FLOAT_EQ(rifle.recoil_recovery_per_second, 0.2F);
  EXPECT_FLOAT_EQ(rifle.ads_recoil_scale, 0.5F);
  EXPECT_FLOAT_EQ(rifle.ads_field_of_view, 0.7F);
  EXPECT_FLOAT_EQ(loaded->ammo.gravity, 9.81F);
  EXPECT_FLOAT_EQ(loaded->ammo.max_range, 1000.0F);
  EXPECT_FLOAT_EQ(loaded->ammo.damage.head, 100.0F);
  EXPECT_FLOAT_EQ(loaded->ammo.damage.torso, 34.0F);
  EXPECT_FLOAT_EQ(loaded->ammo.damage.limb, 25.0F);
  EXPECT_FLOAT_EQ(loaded->starting_health, 100.0F);
}

TEST(ParametersLoaderCombatTest, AMissingCombatKeyIsAnErrorNamingItsPath) {
  ExpectError(Replacing("starting_health = 100,", ""), LoadErrorCode::kMissingKey, "starting_health");
  ExpectError(Replacing("magazine_capacity = 30,", ""), LoadErrorCode::kMissingKey, "rifle.magazine_capacity");
  ExpectError(Replacing("ads_field_of_view = 0.7,", ""), LoadErrorCode::kMissingKey, "rifle.ads_field_of_view");
  ExpectError(Replacing("recoil_pattern = { { pitch = 0.01, yaw = 0.002 }, { pitch = 0.008, yaw = -0.002 } },", ""),
              LoadErrorCode::kMissingKey, "rifle.recoil_pattern");
  ExpectError(Replacing("{ pitch = 0.01, yaw = 0.002 }", "{ pitch = 0.01 }"), LoadErrorCode::kMissingKey,
              "rifle.recoil_pattern[1].yaw");
  ExpectError(Replacing("damage = { head = 100, torso = 34, limb = 25 },", ""), LoadErrorCode::kMissingKey,
              "ammo.damage");
  ExpectError(Replacing("limb = 25", ""), LoadErrorCode::kMissingKey, "ammo.damage.limb");
}

TEST(ParametersLoaderCombatTest, AWholeTableMissingIsNamed) {
  ExpectError(
      "return { player_count = 1, stamina = { deplete_per_second = 0, regen_per_second = 0, "
      "forced_walk_below = 0 } }",
      LoadErrorCode::kMissingKey, "rifle");
}

TEST(ParametersLoaderCombatTest, AnUnknownCombatKeyIsAnErrorNamingItsPath) {
  ExpectError(Replacing("reload_seconds = 2.5,", "reload_seconds = 2.5, spread = 0.01,"), LoadErrorCode::kUnknownKey,
              "rifle.spread");
  ExpectError(Replacing("limb = 25", "limb = 25, neck = 80"), LoadErrorCode::kUnknownKey, "ammo.damage.neck");
  ExpectError(Replacing("{ pitch = 0.008, yaw = -0.002 }", "{ pitch = 0.008, yaw = -0.002, roll = 1 }"),
              LoadErrorCode::kUnknownKey, "rifle.recoil_pattern[2].roll");
}

TEST(ParametersLoaderCombatTest, ACombatValueOfTheWrongTypeIsAnErrorNamingItsPath) {
  ExpectError(Replacing("magazine_capacity = 30", "magazine_capacity = 30.5"), LoadErrorCode::kWrongType,
              "rifle.magazine_capacity");
  ExpectError(Replacing("muzzle_velocity = 800", "muzzle_velocity = 'fast'"), LoadErrorCode::kWrongType,
              "rifle.muzzle_velocity");
  ExpectError(Replacing("damage = { head = 100, torso = 34, limb = 25 }", "damage = 50"), LoadErrorCode::kWrongType,
              "ammo.damage");
  ExpectError(Replacing("yaw = 0.002", "yaw = '0.002'"), LoadErrorCode::kWrongType, "rifle.recoil_pattern[1].yaw");
  ExpectError(Replacing("{ pitch = 0.008, yaw = -0.002 }", "0.008"), LoadErrorCode::kWrongType,
              "rifle.recoil_pattern[2]");
}

TEST(ParametersLoaderCombatTest, ARecoilPatternThatIsNotAListIsTheWrongType) {
  for (const std::string_view pattern : {"{ pitch = 0.01, yaw = 0 }",
                                         "{ [1] = { pitch = 0, yaw = 0 }, [3] = { "
                                         "pitch = 0, yaw = 0 } }",
                                         "{ [0] = { pitch = 0, yaw = 0 } }", "3"}) {
    ExpectError(Replacing("recoil_pattern = { { pitch = 0.01, yaw = 0.002 }, { pitch = 0.008, yaw = -0.002 } }",
                          "recoil_pattern = " + std::string(pattern)),
                LoadErrorCode::kWrongType, "rifle.recoil_pattern");
  }
}

TEST(ParametersLoaderCombatTest, AnEmptyRecoilPatternIsNoRecoil) {
  const auto loaded = Load(Replacing(
      "recoil_pattern = { { pitch = 0.01, yaw = 0.002 }, { pitch = 0.008, yaw = -0.002 } }", "recoil_pattern = {}"));

  ASSERT_TRUE(loaded.has_value()) << DescribeLoadError(loaded.error());
  EXPECT_TRUE(loaded->rifle.recoil_pattern.empty());
}

TEST(ParametersLoaderCombatTest, ACombatValueOutsideItsRangeIsOutOfRange) {
  ExpectError(Replacing("magazine_capacity = 30", "magazine_capacity = 0"), LoadErrorCode::kOutOfRange,
              "rifle.magazine_capacity");
  ExpectError(Replacing("magazine_capacity = 30", "magazine_capacity = 256"), LoadErrorCode::kOutOfRange,
              "rifle.magazine_capacity");
  ExpectError(Replacing("rounds_per_minute = 600", "rounds_per_minute = 0"), LoadErrorCode::kOutOfRange,
              "rifle.rounds_per_minute");
  ExpectError(Replacing("ads_recoil_scale = 0.5", "ads_recoil_scale = 2"), LoadErrorCode::kOutOfRange,
              "rifle.ads_recoil_scale");
  ExpectError(Replacing("max_range = 1000", "max_range = 0"), LoadErrorCode::kOutOfRange, "ammo.max_range");
  ExpectError(Replacing("torso = 34", "torso = -1"), LoadErrorCode::kOutOfRange, "ammo.damage.torso");
  ExpectError(Replacing("starting_health = 100", "starting_health = 0/0"), LoadErrorCode::kOutOfRange,
              "starting_health");
}

TEST(ParametersLoaderCombatTest, ARecoilPatternLongerThanTheWireCarriesIsOutOfRange) {
  const std::string long_pattern = "local kicks = {}\nfor i = 1, 65 do kicks[i] = { pitch = 0, yaw = 0 } end\n" +
                                   Replacing(
                                       "recoil_pattern = { { pitch = 0.01, yaw = 0.002 }, { pitch = 0.008, "
                                       "yaw = -0.002 } }",
                                       "recoil_pattern = kicks");

  ExpectError(long_pattern, LoadErrorCode::kOutOfRange, "rifle.recoil_pattern");
}

TEST(ParametersLoaderTest, APlayerCountMayBeAnyWholeNumberFromOneToTheMostAMatchHolds) {
  for (const std::string_view count : {"1", "8", "16 / 2"}) {
    EXPECT_TRUE(Load(WithPlayerCount(count)).has_value()) << count;
  }
}

TEST(ParametersLoaderTest, APlayerCountOutsideItsRangeIsOutOfRange) {
  for (const std::string_view count : {"0", "9", "256", "-1", "2^40", "math.huge", "0/0"}) {
    ExpectError(WithPlayerCount(count), LoadErrorCode::kOutOfRange, "player_count");
  }
}

TEST(ParametersLoaderTest, APlayerCountThatIsNotAWholeNumberIsTheWrongType) {
  for (const std::string_view count : {"1.5", "'2'", "true"}) {
    ExpectError(WithPlayerCount(count), LoadErrorCode::kWrongType, "player_count");
  }
}

TEST(ParametersLoaderTest, AcceptsAWholeNumberWrittenWithoutADecimalPoint) {
  const auto loaded = Load(WithStamina("1", "0", "0"));

  ASSERT_TRUE(loaded.has_value());
  EXPECT_FLOAT_EQ(loaded->stamina.deplete_per_second, 1.0F);
  EXPECT_FLOAT_EQ(loaded->stamina.regen_per_second, 0.0F);
}

TEST(ParametersLoaderTest, ASyntaxErrorIsAScriptError) {
  const auto loaded = Load("return {");

  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, LoadErrorCode::kScriptError);
  EXPECT_FALSE(loaded.error().subject.empty());
}

TEST(ParametersLoaderTest, AnErrorRaisedByTheScriptIsAScriptErrorCarryingItsMessage) {
  const auto loaded = Load("error('no such thing')");

  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, LoadErrorCode::kScriptError);
  EXPECT_NE(loaded.error().subject.find("no such thing"), std::string::npos);
}

TEST(ParametersLoaderTest, AScriptThatDoesNotReturnATableIsNotATable) {
  ExpectError("return 3", LoadErrorCode::kNotATable, "");
  ExpectError("local unused = {}", LoadErrorCode::kNotATable, "");
}

TEST(ParametersLoaderTest, AMissingKeyIsAnErrorNamingItsPath) {
  ExpectError("return {}", LoadErrorCode::kMissingKey, "player_count");
  ExpectError("return { stamina = { deplete_per_second = 0, regen_per_second = 0, forced_walk_below = 0 } }",
              LoadErrorCode::kMissingKey, "player_count");
  ExpectError("return { player_count = 1 }", LoadErrorCode::kMissingKey, "stamina");
  ExpectError("return { player_count = 1, stamina = { regen_per_second = 0, forced_walk_below = 0 } }",
              LoadErrorCode::kMissingKey, "stamina.deplete_per_second");
  ExpectError("return { player_count = 1, stamina = { deplete_per_second = 0, forced_walk_below = 0 } }",
              LoadErrorCode::kMissingKey, "stamina.regen_per_second");
  ExpectError("return { player_count = 1, stamina = { deplete_per_second = 0, regen_per_second = 0 } }",
              LoadErrorCode::kMissingKey, "stamina.forced_walk_below");
}

TEST(ParametersLoaderTest, AnUnknownKeyIsAnErrorNamingItsPath) {
  ExpectError(
      "return { stamina = { deplete_per_second = 0, regen_per_second = 0, forced_walk_below = 0 }, "
      "weapon = 1 }",
      LoadErrorCode::kUnknownKey, "weapon");
  ExpectError(
      "return { player_count = 1, stamina = { deplete_per_second = 0, regen_per_second = 0, forced_walk_below = 0, "
      "regen_per_sec = 1 } }",
      LoadErrorCode::kUnknownKey, "stamina.regen_per_sec");
}

TEST(ParametersLoaderTest, TheTickRateIsNotAParameterAndIsAnUnknownKey) {
  // It is the server's startup setting (ADR-0034), fixed while the server runs.
  ExpectError(
      "return { tick_rate_hz = 60, stamina = { deplete_per_second = 0, regen_per_second = 0, forced_walk_below = 0 } }",
      LoadErrorCode::kUnknownKey, "tick_rate_hz");
}

TEST(ParametersLoaderTest, AMisspelledKeyIsNeverReadAsAMissingOneOrADefault) {
  // The misspelling is the cause, so it is what is reported, not the key it left absent.
  ExpectError(
      "return { player_count = 1, stamina = { deplete_per_second = 0, regen_per_secnd = 0, forced_walk_below = 0 } }",
      LoadErrorCode::kUnknownKey, "stamina.regen_per_secnd");
}

TEST(ParametersLoaderTest, WhenSeveralKeysAreUnknownTheFirstInNameOrderIsReported) {
  ExpectError("return { zz = 1, aa = 2, stamina = {} }", LoadErrorCode::kUnknownKey, "aa");
}

TEST(ParametersLoaderTest, AValueOfTheWrongTypeIsAnErrorNamingItsPath) {
  ExpectError(WithStamina("'0.5'", "0", "0"), LoadErrorCode::kWrongType, "stamina.deplete_per_second");
  ExpectError(WithStamina("0", "true", "0"), LoadErrorCode::kWrongType, "stamina.regen_per_second");
  ExpectError(WithStamina("0", "0", "{}"), LoadErrorCode::kWrongType, "stamina.forced_walk_below");
  ExpectError("return { player_count = 1, stamina = 3 }", LoadErrorCode::kWrongType, "stamina");
}

TEST(ParametersLoaderTest, ANegativeRateIsOutOfRange) {
  ExpectError(WithStamina("-0.1", "0", "0"), LoadErrorCode::kOutOfRange, "stamina.deplete_per_second");
  ExpectError(WithStamina("0", "-0.1", "0"), LoadErrorCode::kOutOfRange, "stamina.regen_per_second");
}

TEST(ParametersLoaderTest, ANumberThatIsNotFiniteIsOutOfRange) {
  ExpectError(WithStamina("math.huge", "0", "0"), LoadErrorCode::kOutOfRange, "stamina.deplete_per_second");
  ExpectError(WithStamina("0", "0/0", "0"), LoadErrorCode::kOutOfRange, "stamina.regen_per_second");
  ExpectError(WithStamina("0", "0", "0/0"), LoadErrorCode::kOutOfRange, "stamina.forced_walk_below");
}

TEST(ParametersLoaderTest, TheForcedWalkThresholdIsAtLeastZeroAndBelowOne) {
  EXPECT_TRUE(Load(WithStamina("0", "0", "0")).has_value());
  ExpectError(WithStamina("0", "0", "-0.1"), LoadErrorCode::kOutOfRange, "stamina.forced_walk_below");
  ExpectError(WithStamina("0", "0", "1"), LoadErrorCode::kOutOfRange, "stamina.forced_walk_below");
}

// Every name below is something the sandbox does not give a script (ADR-0039):
// the filesystem, the process, the clock, randomness, and loading more code.
constexpr std::string_view kUnavailable[] = {
    "io",   "os",    "package",  "debug",       "coroutine",       "require", "dofile",
    "load", "print", "loadfile", "math.random", "math.randomseed", "pcall",   "xpcall",
};

TEST(ParametersLoaderSandboxTest, NothingThatReachesOutsideTheScriptIsVisible) {
  for (const std::string_view name : kUnavailable) {
    const std::string script = "if " + std::string(name) + " ~= nil then error('visible') end\n" + std::string(kValid);
    EXPECT_TRUE(Load(script).has_value()) << name;
  }
}

TEST(ParametersLoaderSandboxTest, AScriptThatCallsWhatTheSandboxLacksFailsToLoad) {
  for (const std::string_view call :
       {"io.open('parameters.txt')", "os.execute('echo')", "os.time()", "os.clock()", "math.random()",
        "package.loadlib('x', 'y')", "dofile('other.lua')", "load('return 1')()", "require('other')"}) {
    const auto loaded = Load("local unused = " + std::string(call) + "\n" + std::string(kValid));

    ASSERT_FALSE(loaded.has_value()) << call;
    EXPECT_EQ(loaded.error().code, LoadErrorCode::kScriptError) << call;
  }
}

TEST(ParametersLoaderSandboxTest, AScriptThatNeverReturnsIsStoppedByTheInstructionLimit) {
  for (const std::string_view loop : {"while true do end", "repeat until false", "for i = 1, math.huge do end"}) {
    const auto loaded = Load(loop);

    ASSERT_FALSE(loaded.has_value()) << loop;
    EXPECT_EQ(loaded.error().code, LoadErrorCode::kScriptError) << loop;
    EXPECT_NE(loaded.error().subject.find("instruction limit"), std::string::npos) << loaded.error().subject;
  }
}

TEST(ParametersLoaderSandboxTest, AScriptCannotCatchTheInstructionLimitAndRunOn) {
  // Were pcall there, each stop would be caught and the loop would go on for ever.
  for (const std::string_view loop : {"while true do pcall(function() while true do end end) end",
                                      "while true do xpcall(function() while true do end end, print) end"}) {
    const auto loaded = Load(loop);

    ASSERT_FALSE(loaded.has_value()) << loop;
    EXPECT_EQ(loaded.error().code, LoadErrorCode::kScriptError) << loop;
  }
}

TEST(ParametersLoaderSandboxTest, AScriptThatDoesRealWorkWithinTheLimitStillLoads) {
  const auto loaded = Load(
      "local sum = 0\n"
      "for i = 1, 10000 do sum = sum + i end\n" +
      Complete("player_count = 1, stamina = { deplete_per_second = sum / 50005000, regen_per_second = 0, "
               "forced_walk_below = 0 }"));

  ASSERT_TRUE(loaded.has_value());
  EXPECT_FLOAT_EQ(loaded->stamina.deplete_per_second, 1.0F);
}

TEST(ParametersLoaderSandboxTest, EachLoadHasAStateOfItsOwn) {
  ASSERT_TRUE(Load("leaked = 1\n" + std::string(kValid)).has_value());

  EXPECT_TRUE(Load("if leaked ~= nil then error('shared') end\n" + std::string(kValid)).has_value());
}

TEST(ParametersLoaderExpressionTest, AValueMayBeAnExpressionOfOtherValuesInTheScript) {
  const auto loaded = Load(
      "local sprint_seconds = 5\n"
      "local rest_seconds = 10\n" +
      Complete("player_count = 1, stamina = {\n"
               "  deplete_per_second = 1 / sprint_seconds,\n"
               "  regen_per_second = 1 / rest_seconds,\n"
               "  forced_walk_below = 0.5 / sprint_seconds,\n"
               "}"));

  ASSERT_TRUE(loaded.has_value());
  EXPECT_FLOAT_EQ(loaded->stamina.deplete_per_second, 0.2F);
  EXPECT_FLOAT_EQ(loaded->stamina.regen_per_second, 0.1F);
  EXPECT_FLOAT_EQ(loaded->stamina.forced_walk_below, 0.1F);
}

TEST(ParametersLoaderExpressionTest, AnExpressionCanUseAFunctionOfTheScript) {
  const auto loaded =
      Load("local function per_second(seconds) return 1 / seconds end\n" +
           Complete("player_count = 1, stamina = { deplete_per_second = per_second(4), regen_per_second = "
                    "math.sqrt(0.25),\n"
                    "  forced_walk_below = 0 }"));

  ASSERT_TRUE(loaded.has_value());
  EXPECT_FLOAT_EQ(loaded->stamina.deplete_per_second, 0.25F);
  EXPECT_FLOAT_EQ(loaded->stamina.regen_per_second, 0.5F);
}

TEST(ParametersLoaderExpressionTest, AnExpressionThatIsOutOfRangeIsRefusedLikeAnyValue) {
  ExpectError(WithStamina("1 / 0", "0", "0"), LoadErrorCode::kOutOfRange, "stamina.deplete_per_second");
}

TEST(ParametersLoaderExpressionTest, TheSameScriptLoadedTwiceGivesEqualParameters) {
  const std::string script = "local base = 0.3\n" + WithStamina("base * 2", "base / 3", "base / 4");

  const auto first = Load(script);
  const auto second = Load(script);

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->player_count, second->player_count);
  EXPECT_EQ(first->stamina.deplete_per_second, second->stamina.deplete_per_second);
  EXPECT_EQ(first->stamina.regen_per_second, second->stamina.regen_per_second);
  EXPECT_EQ(first->stamina.forced_walk_below, second->stamina.forced_walk_below);
}

TEST(ParametersLoaderTest, ADescriptionNamesTheKeyItIsAbout) {
  const std::string message =
      DescribeLoadError({.code = LoadErrorCode::kOutOfRange, .subject = "stamina.regen_per_second"});

  EXPECT_NE(message.find("stamina.regen_per_second"), std::string::npos) << message;
}

// The example scenario's script (tools/pack/examples/authoring/scenarios/augusta)
// is what an author copies to start a new one, so it must stay a script the
// loader accepts.
TEST(ParametersExampleTest, TheExampleScriptLoadsToTheDocumentedDefaults) {
  std::ifstream stream(AUGUSTA_EXAMPLE_PARAMETERS, std::ios::binary);
  ASSERT_TRUE(stream.is_open()) << AUGUSTA_EXAMPLE_PARAMETERS;
  std::ostringstream script;
  script << stream.rdbuf();

  const auto loaded = Load(script.str());

  ASSERT_TRUE(loaded.has_value()) << DescribeLoadError(loaded.error());
  EXPECT_EQ(loaded->player_count, 1U);
  EXPECT_FLOAT_EQ(loaded->stamina.deplete_per_second, 0.2F);
  EXPECT_FLOAT_EQ(loaded->stamina.regen_per_second, 0.1F);
  EXPECT_FLOAT_EQ(loaded->stamina.forced_walk_below, 0.1F);
  EXPECT_EQ(loaded->rifle.magazine_capacity, 30U);
  EXPECT_FLOAT_EQ(loaded->rifle.rounds_per_minute, 600.0F);
  EXPECT_FLOAT_EQ(loaded->rifle.muzzle_velocity, 800.0F);
  EXPECT_EQ(loaded->rifle.recoil_pattern.size(), 30U);
  EXPECT_FLOAT_EQ(loaded->ammo.max_range, 1000.0F);
  EXPECT_FLOAT_EQ(loaded->ammo.damage.head, loaded->starting_health);
  EXPECT_FLOAT_EQ(loaded->starting_health, 100.0F);
}

}  // namespace
