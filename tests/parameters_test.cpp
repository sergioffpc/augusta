#include "augusta/parameters.h"

#include <cstdint>
#include <limits>
#include <numbers>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/protocol.h"

// The decisions about Parameters that the server and every client share (ADR-0039)
// are pure: values in, a verdict out.
namespace {

using augusta::parameters::IsValidTickRate;
using augusta::parameters::Parameters;
using augusta::parameters::Validate;

const Parameters kUsable{.stamina = {.deplete_per_second = 0.2F, .regen_per_second = 0.1F, .forced_walk_below = 0.1F},
                         .player_count = 1};

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

TEST(ValidateTest, DefaultParametersPass) { EXPECT_TRUE(Validate(Parameters{}).has_value()); }

// One value of a Parameters, set to something the simulation cannot run on, and
// the path Validate must name for it.
struct BadValue {
  void (*spoil)(Parameters&);
  std::string_view path;
};

TEST(ValidateTest, EachRifleAmmoOrHealthValueOutsideItsRangeIsNamedByItsPath) {
  constexpr float kInfinity = std::numeric_limits<float>::infinity();
  constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
  const std::vector<BadValue> cases{
      {[](Parameters& p) { p.rifle.rounds_per_minute = 0.0F; }, "rifle.rounds_per_minute"},
      {[](Parameters& p) { p.rifle.rounds_per_minute = kInfinity; }, "rifle.rounds_per_minute"},
      {[](Parameters& p) { p.rifle.muzzle_velocity = -1.0F; }, "rifle.muzzle_velocity"},
      {[](Parameters& p) { p.rifle.reload_seconds = -0.5F; }, "rifle.reload_seconds"},
      {[](Parameters& p) { p.rifle.recoil_recovery_per_second = kNan; }, "rifle.recoil_recovery_per_second"},
      {[](Parameters& p) { p.rifle.ads_recoil_scale = 0.0F; }, "rifle.ads_recoil_scale"},
      {[](Parameters& p) { p.rifle.ads_recoil_scale = 1.5F; }, "rifle.ads_recoil_scale"},
      {[](Parameters& p) { p.rifle.ads_field_of_view = 0.0F; }, "rifle.ads_field_of_view"},
      {[](Parameters& p) { p.rifle.ads_field_of_view = std::numbers::pi_v<float>; }, "rifle.ads_field_of_view"},
      {[](Parameters& p) { p.rifle.recoil_pattern = {{.pitch = kNan, .yaw = 0.0F}}; }, "rifle.recoil_pattern"},
      {[](Parameters& p) { p.rifle.recoil_pattern.resize(augusta::protocol::kMaxRecoilKicks + 1); },
       "rifle.recoil_pattern"},
      {[](Parameters& p) { p.rifle.magazine_capacity = 0; }, "rifle.magazine_capacity"},
      {[](Parameters& p) { p.ammo.gravity = -9.81F; }, "ammo.gravity"},
      {[](Parameters& p) { p.ammo.max_range = 0.0F; }, "ammo.max_range"},
      {[](Parameters& p) { p.ammo.damage.head = -1.0F; }, "ammo.damage.head"},
      {[](Parameters& p) { p.ammo.damage.torso = kInfinity; }, "ammo.damage.torso"},
      {[](Parameters& p) { p.ammo.damage.limb = kNan; }, "ammo.damage.limb"},
      {[](Parameters& p) { p.starting_health = 0.0F; }, "starting_health"},
  };
  for (const BadValue& bad : cases) {
    Parameters parameters = kUsable;
    bad.spoil(parameters);

    const auto valid = Validate(parameters);

    ASSERT_FALSE(valid.has_value()) << bad.path;
    EXPECT_EQ(valid.error().path, bad.path);
  }
}

TEST(ValidateTest, ARecoilPatternMayBeEmptyOrHoldTheMostKicksTheWireCarries) {
  Parameters none = kUsable;
  none.rifle.recoil_pattern.clear();
  Parameters most = kUsable;
  most.rifle.recoil_pattern.assign(augusta::protocol::kMaxRecoilKicks, {.pitch = 0.01F, .yaw = -0.002F});

  EXPECT_TRUE(Validate(none).has_value());
  EXPECT_TRUE(Validate(most).has_value());
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
