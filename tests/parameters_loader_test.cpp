#include "augusta/parameters_loader.h"

#include <expected>
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
      "return { stamina = { deplete_per_second = 0, regen_per_second = 0, forced_walk_below = 0 }, recoil = 1 }",
      LoadErrorCode::kUnknownKey, "recoil");
  ExpectError(
      "return { stamina = { deplete_per_second = 0, regen_per_second = 0, forced_walk_below = 0, regen_per_sec = 1 } }",
      LoadErrorCode::kUnknownKey, "stamina.regen_per_sec");
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

TEST(ParametersLoaderTest, ADescriptionNamesTheKeyItIsAbout) {
  const std::string message =
      DescribeLoadError({.code = LoadErrorCode::kOutOfRange, .subject = "stamina.regen_per_second"});

  EXPECT_NE(message.find("stamina.regen_per_second"), std::string::npos) << message;
}

}  // namespace
