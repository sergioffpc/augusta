#include "augusta/parameters.h"

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

// The decisions about Parameters that the server and every client share (ADR-0039)
// are pure: values in, a verdict out.
namespace {

using augusta::parameters::CheckReplacement;
using augusta::parameters::KeepsTickRate;
using augusta::parameters::NumberedParameters;
using augusta::parameters::Parameters;
using augusta::parameters::ReplacementRefusal;
using augusta::parameters::Validate;

constexpr Parameters kUsable{
    .tick_rate_hz = 60.0F,
    .stamina = {.deplete_per_second = 0.2F, .regen_per_second = 0.1F, .forced_walk_below = 0.1F}};

Parameters WithTickRate(float tick_rate_hz) {
  Parameters parameters = kUsable;
  parameters.tick_rate_hz = tick_rate_hz;
  return parameters;
}

TEST(ValidateTest, UsableParametersPass) { EXPECT_TRUE(Validate(kUsable).has_value()); }

TEST(ValidateTest, ParametersNeverSetAreRefusedForTheTickRateFirst) {
  const auto valid = Validate(Parameters{});

  ASSERT_FALSE(valid.has_value());
  EXPECT_EQ(valid.error().path, "tick_rate_hz");
}

TEST(ValidateTest, EachValueOutsideItsRangeIsNamedByItsPath) {
  Parameters negative_deplete = kUsable;
  negative_deplete.stamina.deplete_per_second = -0.1F;
  Parameters infinite_regen = kUsable;
  infinite_regen.stamina.regen_per_second = std::numeric_limits<float>::infinity();
  Parameters threshold_of_one = kUsable;
  threshold_of_one.stamina.forced_walk_below = 1.0F;

  EXPECT_EQ(Validate(WithTickRate(-1.0F)).error().path, "tick_rate_hz");
  EXPECT_EQ(Validate(negative_deplete).error().path, "stamina.deplete_per_second");
  EXPECT_EQ(Validate(infinite_regen).error().path, "stamina.regen_per_second");
  EXPECT_EQ(Validate(threshold_of_one).error().path, "stamina.forced_walk_below");
}

TEST(KeepsTickRateTest, OnlyTheSameRateIsKept) {
  EXPECT_TRUE(KeepsTickRate(WithTickRate(60.0F), WithTickRate(60.0F)));
  EXPECT_FALSE(KeepsTickRate(WithTickRate(60.0F), WithTickRate(30.0F)));
}

NumberedParameters Numbered(std::uint32_t generation, const Parameters& parameters = kUsable) {
  return NumberedParameters{.generation = generation, .parameters = parameters};
}

TEST(CheckReplacementTest, ANewerUsableGenerationAtTheSameRateMayReplace) {
  EXPECT_TRUE(CheckReplacement(Numbered(1), Numbered(2)).has_value());
  EXPECT_TRUE(CheckReplacement(Numbered(1), Numbered(9)).has_value());
}

TEST(CheckReplacementTest, AGenerationThatIsNotNewerMayNot) {
  for (const std::uint32_t generation : {0U, 1U, 4U, 5U}) {
    const auto checked = CheckReplacement(Numbered(5), Numbered(generation));

    ASSERT_FALSE(checked.has_value()) << generation;
    EXPECT_EQ(checked.error().reason, ReplacementRefusal::kNotNewer) << generation;
  }
}

TEST(CheckReplacementTest, ValuesTheSimulationCannotRunOnMayNotAndTheParameterIsNamed) {
  Parameters bad = kUsable;
  bad.stamina.regen_per_second = -1.0F;

  const auto checked = CheckReplacement(Numbered(1), Numbered(2, bad));

  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().reason, ReplacementRefusal::kInvalid);
  EXPECT_EQ(checked.error().parameter, "stamina.regen_per_second");
}

TEST(CheckReplacementTest, AnotherTickRateMayNot) {
  const auto checked = CheckReplacement(Numbered(1), Numbered(2, WithTickRate(30.0F)));

  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().reason, ReplacementRefusal::kTickRateChanged);
}

TEST(CheckReplacementTest, AGenerationThatIsNotNewerIsRefusedBeforeItsValuesAreLookedAt) {
  const auto checked = CheckReplacement(Numbered(3), Numbered(2, Parameters{}));

  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().reason, ReplacementRefusal::kNotNewer);
}

}  // namespace
