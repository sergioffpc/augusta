#include "augusta/parameters.h"

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

// The decisions about Parameters that the server and every client share (ADR-0039)
// are pure: values in, a verdict out.
namespace {

using augusta::parameters::IsValidTickRate;
using augusta::parameters::Parameters;
using augusta::parameters::Validate;

constexpr Parameters kUsable{
    .stamina = {.deplete_per_second = 0.2F, .regen_per_second = 0.1F, .forced_walk_below = 0.1F}, .player_count = 1};

TEST(ValidateTest, UsableParametersPass) { EXPECT_TRUE(Validate(kUsable).has_value()); }

TEST(ValidateTest, EachValueOutsideItsRangeIsNamedByItsPath) {
  Parameters negative_deplete = kUsable;
  negative_deplete.stamina.deplete_per_second = -0.1F;
  Parameters infinite_regen = kUsable;
  infinite_regen.stamina.regen_per_second = std::numeric_limits<float>::infinity();
  Parameters threshold_of_one = kUsable;
  threshold_of_one.stamina.forced_walk_below = 1.0F;

  EXPECT_EQ(Validate(negative_deplete).error().path, "stamina.deplete_per_second");
  EXPECT_EQ(Validate(infinite_regen).error().path, "stamina.regen_per_second");
  EXPECT_EQ(Validate(threshold_of_one).error().path, "stamina.forced_walk_below");
}

TEST(ValidateTest, APlayerCountFromOneToTheMostAMatchHoldsPasses) {
  for (const std::uint8_t count : {std::uint8_t{1}, std::uint8_t{augusta::protocol::kMaxPlayers}}) {
    Parameters parameters = kUsable;
    parameters.player_count = count;

    EXPECT_TRUE(Validate(parameters).has_value()) << static_cast<int>(count);
  }
}

TEST(ValidateTest, APlayerCountOfZeroOrAboveTheMostAMatchHoldsIsNamed) {
  for (const std::uint8_t count :
       {std::uint8_t{0}, std::uint8_t{augusta::protocol::kMaxPlayers + 1}, std::numeric_limits<std::uint8_t>::max()}) {
    Parameters parameters = kUsable;
    parameters.player_count = count;

    EXPECT_EQ(Validate(parameters).error().path, "player_count") << static_cast<int>(count);
  }
}

TEST(IsValidTickRateTest, IntegerRatesFromOneTo255AreValid) {
  for (const std::uint8_t rate :
       {std::uint8_t{1}, std::uint8_t{30}, std::uint8_t{60}, std::uint8_t{240}, std::uint8_t{255}}) {
    EXPECT_TRUE(IsValidTickRate(rate)) << rate;
  }
}

TEST(IsValidTickRateTest, ZeroIsNotValid) {
  for (const std::uint8_t rate : {std::uint8_t{0}}) {
    EXPECT_FALSE(IsValidTickRate(rate)) << rate;
  }
}

}  // namespace
