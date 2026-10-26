#include <cmath>
#include <cstdint>
#include <filesystem>
#include <utility>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/content.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"
#include "augusta/simulation_mapping.h"

// The example scenario's soldier holding the example rifle, as the server poses
// it from the golden server pack (ADR-0064, ADR-0066): its ADS grip shoulders
// the rifle with its Sight on the Eye, and its hip hold carries it ahead of the
// body, so a round aimed down sights lands on what it is aimed at at the
// example Map's ranges.
namespace {

using augusta::math::Vec3;
using augusta::physics::Stance;
using augusta::simulation::EntityId;

const std::filesystem::path kPacks{AUGUSTA_EXAMPLE_PACKS};
constexpr std::uint8_t kTickRateHz = 60;
constexpr float kTick = 1.0F / kTickRateHz;
// Enough ticks for a spawned body to land and settle into any stance.
constexpr int kSettleTicks = 120;

augusta::scenario::Content ExampleContent() {
  const auto key = augusta::assets::ReadEd25519PublicKeyFile(kPacks / "test.pub");
  EXPECT_TRUE(key.has_value());
  auto pack = augusta::assets::Pack::Load((kPacks / "server").replace_extension(augusta::assets::kPackExtension), *key);
  EXPECT_TRUE(pack.has_value());
  auto content = augusta::scenario::LoadServerContent(*pack, kTickRateHz);
  EXPECT_TRUE(content.has_value()) << content.error().detail;
  return *std::move(content);
}

class ExampleSoldierTest : public ::testing::Test {
 protected:
  ExampleSoldierTest()
      : content_(ExampleContent()),
        soldier_(augusta::host::ToSimulation(content_.scenario.characters).at("soldier")),
        world_(augusta::host::BuildSimulation(content_.parameters, kTickRateHz, content_.scenario, {})) {}

  // Where body sees from: its feet, and the soldier's Eye lowered for its
  // stance.
  [[nodiscard]] Vec3 EyeOf(const augusta::simulation::EntityState& body) const {
    return body.body.position + augusta::physics::LowerToStance(soldier_.eye, body.body.stance);
  }

  // Runs ticks of command for entity alone; returns the last tick's state.
  augusta::simulation::State Hold(EntityId entity, const augusta::command::Command& command, int ticks) {
    augusta::simulation::State state;
    for (int tick = 0; tick < ticks; ++tick) {
      state = world_.Tick({{.entity = entity, .command = command}}, kTick).state;
    }
    return state;
  }

  augusta::scenario::Content content_;
  augusta::simulation::Character soldier_;
  augusta::simulation::World world_;
};

// The Sight on the line of sight from the Eye, a little ahead of it, and the
// rifle along the view, in every stance.
// Requirements: US-10
TEST_F(ExampleSoldierTest, AimingDownSightsShouldersTheRifleWithItsSightOnTheEye) {
  constexpr float kOffTheLine = 0.02F;
  constexpr float kAheadAtMost = 0.2F;
  const EntityId soldier{1};
  world_.AddPlayer(soldier, Vec3(0.0F, 0.1F, 0.0F), soldier_);

  for (const Stance stance : {Stance::kStanding, Stance::kCrouching, Stance::kProne}) {
    augusta::command::Command command;
    command.ads = true;
    command.movement.desired_stance = stance;
    const augusta::simulation::State state = Hold(soldier, command, kSettleTicks);

    ASSERT_EQ(state.bodies.size(), 1U);
    ASSERT_EQ(state.bodies[0].body.stance, stance);
    const auto held = world_.HeldRifleOf(soldier);
    ASSERT_TRUE(held.has_value());
    const Vec3 view = augusta::command::ViewDirection(0.0F, 0.0F);
    const Vec3 from_eye = Vec3(held->sight[3]) - EyeOf(state.bodies[0]);
    const float ahead = augusta::math::Dot(from_eye, view);
    EXPECT_LE(augusta::math::Length(from_eye - (view * ahead)), kOffTheLine) << static_cast<int>(stance);
    EXPECT_GE(ahead, 0.0F) << static_cast<int>(stance);
    EXPECT_LE(ahead, kAheadAtMost) << static_cast<int>(stance);
    EXPECT_GT(augusta::math::Dot(-Vec3(held->sight[2]), view), 0.999F) << static_cast<int>(stance);
  }
}

// From the hip, standing, the rifle is held level along the view, ahead of the
// body and below the Eye, not hanging at the soldier's side.
// Requirements: US-10
TEST_F(ExampleSoldierTest, FromTheHipTheRifleIsHeldAheadOfTheBodyAlongTheView) {
  constexpr float kMuzzleAheadAtLeast = 0.8F;
  constexpr float kBelowTheEyeAtLeast = 0.3F;
  const EntityId soldier{1};
  world_.AddPlayer(soldier, Vec3(0.0F, 0.1F, 0.0F), soldier_);

  const augusta::simulation::State state = Hold(soldier, augusta::command::Command{}, kSettleTicks);

  ASSERT_EQ(state.bodies.size(), 1U);
  const auto held = world_.HeldRifleOf(soldier);
  ASSERT_TRUE(held.has_value());
  const Vec3 view = augusta::command::ViewDirection(0.0F, 0.0F);
  const Vec3 from_eye = Vec3(held->muzzle[3]) - EyeOf(state.bodies[0]);
  EXPECT_GE(augusta::math::Dot(from_eye, view), kMuzzleAheadAtLeast);
  EXPECT_LE(from_eye.y, -kBelowTheEyeAtLeast);
  EXPECT_GT(augusta::math::Dot(-Vec3(held->muzzle[2]), view), 0.999F);
}

// Two soldiers at two of the example Map's Spawn points, one aiming down sights
// at the other's torso as an Agent's AimAt does, from where it estimates its
// own Eye: its round strikes.
// Requirements: US-11
TEST_F(ExampleSoldierTest, ARoundAimedDownSightsAtASoldierAcrossTheExampleMapStrikesIt) {
  constexpr int kFlightTicks = 30;
  const EntityId shooter{1};
  const EntityId target{2};
  world_.AddPlayer(shooter, Vec3(0.0F, 0.1F, 5.0F), soldier_);
  world_.AddPlayer(target, Vec3(5.0F, 0.1F, 0.0F), soldier_);
  augusta::simulation::State state;
  for (int tick = 0; tick < kSettleTicks; ++tick) {
    state = world_.Tick({}, kTick).state;
  }
  ASSERT_EQ(state.bodies.size(), 2U);
  const float height = augusta::physics::StanceHeight(Stance::kStanding);
  const Vec3 eye = state.bodies[0].body.position + Vec3(0.0F, height * 0.9F, 0.0F);
  const Vec3 torso = state.bodies[1].body.position + Vec3(0.0F, height * 0.6F, 0.0F);
  const Vec3 toward = torso - eye;
  augusta::command::Command command;
  command.yaw = std::atan2(-toward.x, -toward.z);
  command.pitch = std::atan2(toward.y, std::hypot(toward.x, toward.z));
  command.ads = true;
  command.seen_tick = state.tick;
  // A tick to raise the rifle, then one round.
  static_cast<void>(world_.Tick({{.entity = shooter, .command = command}}, kTick));
  command.fire = true;

  bool struck = false;
  for (int tick = 0; tick < kFlightTicks && !struck; ++tick) {
    const augusta::simulation::TickResult result = world_.Tick({{.entity = shooter, .command = command}}, kTick);
    command.fire = false;
    for (const augusta::simulation::Hit& hit : result.state.hits) {
      struck = struck || (hit.shooter == shooter && hit.target == target);
    }
  }

  EXPECT_TRUE(struck);
}

}  // namespace
