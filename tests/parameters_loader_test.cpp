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

constexpr std::string_view kValid = R"(
return {
  stamina = {
    deplete_per_second = 0.2,
    regen_per_second = 0.1,
    forced_walk_below = 0.05,
  },
}
)";

// A complete script whose three stamina values are the given Lua expressions.
std::string WithStamina(std::string_view deplete, std::string_view regen, std::string_view forced_walk_below) {
  return "return { stamina = { deplete_per_second = " + std::string(deplete) +
         ", regen_per_second = " + std::string(regen) + ", forced_walk_below = " + std::string(forced_walk_below) +
         " } }";
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
  EXPECT_FLOAT_EQ(loaded->stamina.deplete_per_second, 0.2F);
  EXPECT_FLOAT_EQ(loaded->stamina.regen_per_second, 0.1F);
  EXPECT_FLOAT_EQ(loaded->stamina.forced_walk_below, 0.05F);
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
  ExpectError("return {}", LoadErrorCode::kMissingKey, "stamina");
  ExpectError("return { stamina = { regen_per_second = 0, forced_walk_below = 0 } }", LoadErrorCode::kMissingKey,
              "stamina.deplete_per_second");
  ExpectError("return { stamina = { deplete_per_second = 0, forced_walk_below = 0 } }", LoadErrorCode::kMissingKey,
              "stamina.regen_per_second");
  ExpectError("return { stamina = { deplete_per_second = 0, regen_per_second = 0 } }", LoadErrorCode::kMissingKey,
              "stamina.forced_walk_below");
}

TEST(ParametersLoaderTest, AnUnknownKeyIsAnErrorNamingItsPath) {
  ExpectError(
      "return { stamina = { deplete_per_second = 0, regen_per_second = 0, forced_walk_below = 0 }, "
      "recoil = 1 }",
      LoadErrorCode::kUnknownKey, "recoil");
  ExpectError(
      "return { stamina = { deplete_per_second = 0, regen_per_second = 0, forced_walk_below = 0, "
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
  ExpectError("return { stamina = { deplete_per_second = 0, regen_per_secnd = 0, forced_walk_below = 0 } }",
              LoadErrorCode::kUnknownKey, "stamina.regen_per_secnd");
}

TEST(ParametersLoaderTest, WhenSeveralKeysAreUnknownTheFirstInNameOrderIsReported) {
  ExpectError("return { zz = 1, aa = 2, stamina = {} }", LoadErrorCode::kUnknownKey, "aa");
}

TEST(ParametersLoaderTest, AValueOfTheWrongTypeIsAnErrorNamingItsPath) {
  ExpectError(WithStamina("'0.5'", "0", "0"), LoadErrorCode::kWrongType, "stamina.deplete_per_second");
  ExpectError(WithStamina("0", "true", "0"), LoadErrorCode::kWrongType, "stamina.regen_per_second");
  ExpectError(WithStamina("0", "0", "{}"), LoadErrorCode::kWrongType, "stamina.forced_walk_below");
  ExpectError("return { stamina = 3 }", LoadErrorCode::kWrongType, "stamina");
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
      "for i = 1, 10000 do sum = sum + i end\n"
      "return { stamina = { deplete_per_second = sum / 50005000, regen_per_second = 0, "
      "forced_walk_below = 0 } }");

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
      "local rest_seconds = 10\n"
      "return { stamina = {\n"
      "  deplete_per_second = 1 / sprint_seconds,\n"
      "  regen_per_second = 1 / rest_seconds,\n"
      "  forced_walk_below = 0.5 / sprint_seconds,\n"
      "} }");

  ASSERT_TRUE(loaded.has_value());
  EXPECT_FLOAT_EQ(loaded->stamina.deplete_per_second, 0.2F);
  EXPECT_FLOAT_EQ(loaded->stamina.regen_per_second, 0.1F);
  EXPECT_FLOAT_EQ(loaded->stamina.forced_walk_below, 0.1F);
}

TEST(ParametersLoaderExpressionTest, AnExpressionCanUseAFunctionOfTheScript) {
  const auto loaded = Load(
      "local function per_second(seconds) return 1 / seconds end\n"
      "return { stamina = { deplete_per_second = per_second(4), regen_per_second = "
      "math.sqrt(0.25),\n"
      "  forced_walk_below = 0 } }");

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
  EXPECT_EQ(first->stamina.deplete_per_second, second->stamina.deplete_per_second);
  EXPECT_EQ(first->stamina.regen_per_second, second->stamina.regen_per_second);
  EXPECT_EQ(first->stamina.forced_walk_below, second->stamina.forced_walk_below);
}

TEST(ParametersLoaderTest, ADescriptionNamesTheKeyItIsAbout) {
  const std::string message =
      DescribeLoadError({.code = LoadErrorCode::kOutOfRange, .subject = "stamina.regen_per_second"});

  EXPECT_NE(message.find("stamina.regen_per_second"), std::string::npos) << message;
}

// The example script is what an author copies next to a scenario's stage, so it
// must stay a script the loader accepts.
TEST(ParametersExampleTest, TheExampleScriptLoadsToTheDocumentedDefaults) {
  std::ifstream stream(AUGUSTA_EXAMPLE_PARAMETERS, std::ios::binary);
  ASSERT_TRUE(stream.is_open()) << AUGUSTA_EXAMPLE_PARAMETERS;
  std::ostringstream script;
  script << stream.rdbuf();

  const auto loaded = Load(script.str());

  ASSERT_TRUE(loaded.has_value()) << DescribeLoadError(loaded.error());
  EXPECT_FLOAT_EQ(loaded->stamina.deplete_per_second, 0.2F);
  EXPECT_FLOAT_EQ(loaded->stamina.regen_per_second, 0.1F);
  EXPECT_FLOAT_EQ(loaded->stamina.forced_walk_below, 0.1F);
}

}  // namespace
