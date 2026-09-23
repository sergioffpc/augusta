#include "augusta/input.h"

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/physics.h"

// Pure: device events in, one tick's Command out, with no window or GPU.
namespace {

using augusta::input::Action;
using augusta::input::Command;
using augusta::input::Config;
using augusta::input::Input;
using augusta::input::Key;
using augusta::input::MouseMoveEvent;
using augusta::math::Vec3;
using augusta::physics::Stance;

constexpr float kSensitivity = 0.01F;
constexpr float kTolerance = 1e-5F;

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

class InputTest : public ::testing::Test {
 protected:
  void Press(Key key) { input_.OnKeyEvent({.key = key, .action = Action::kPressed}); }
  void Release(Key key) { input_.OnKeyEvent({.key = key, .action = Action::kReleased}); }
  // Moves the cursor by (dx, dy) pixels from where the previous move left it.
  void MoveMouse(float dx, float dy) {
    cursor_x_ += dx;
    cursor_y_ += dy;
    input_.OnMouseMoveEvent(MouseMoveEvent{.x = cursor_x_, .y = cursor_y_});
  }
  // Turns the view by yaw_radians (positive: left), from a cursor already seen once.
  void Turn(float yaw_radians) { MoveMouse(-yaw_radians / kSensitivity, 0.0F); }

  Input input_{Config{.mouse_sensitivity = kSensitivity}};

 private:
  float cursor_x_ = 0.0F;
  float cursor_y_ = 0.0F;
};

TEST_F(InputTest, WithNothingHeldThePlayerStandsStill) {
  const Command command = input_.Sample();
  ExpectNear(command.movement.direction, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_FALSE(command.movement.sprint);
  EXPECT_EQ(command.movement.desired_stance, Stance::kStanding);
}

TEST_F(InputTest, WasdMoveForwardBackLeftAndRightOfAnUnturnedView) {
  Press(Key::kW);
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, -1.0F));
  Release(Key::kW);
  Press(Key::kS);
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, 1.0F));
  Release(Key::kS);
  Press(Key::kA);
  ExpectNear(input_.Sample().movement.direction, Vec3(-1.0F, 0.0F, 0.0F));
  Release(Key::kA);
  Press(Key::kD);
  ExpectNear(input_.Sample().movement.direction, Vec3(1.0F, 0.0F, 0.0F));
}

TEST_F(InputTest, AHeldKeyKeepsMovingUntilItIsReleased) {
  Press(Key::kW);
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, -1.0F));
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, -1.0F));
  Release(Key::kW);
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, 0.0F));
}

TEST_F(InputTest, OppositeKeysCancel) {
  Press(Key::kW);
  Press(Key::kS);
  Press(Key::kA);
  Press(Key::kD);
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, 0.0F));
}

TEST_F(InputTest, ADiagonalIsNoFasterThanAStraightLine) {
  Press(Key::kW);
  Press(Key::kD);
  const Vec3 direction = input_.Sample().movement.direction;
  EXPECT_NEAR(augusta::math::Length(direction), 1.0F, kTolerance);
  ExpectNear(direction, Vec3(std::numbers::sqrt2_v<float> / 2, 0.0F, -std::numbers::sqrt2_v<float> / 2));
}

TEST_F(InputTest, MovementFollowsWhereTheViewHasTurned) {
  MoveMouse(0.0F, 0.0F);
  Turn(std::numbers::pi_v<float> / 2);  // A quarter turn left: now looking down -X.
  Press(Key::kW);
  ExpectNear(input_.Sample().movement.direction, Vec3(-1.0F, 0.0F, 0.0F));
  Release(Key::kW);
  Press(Key::kD);
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, -1.0F));
}

TEST_F(InputTest, ADiagonalFollowsAViewTurnedOffTheAxes) {
  MoveMouse(0.0F, 0.0F);
  Turn(-std::numbers::pi_v<float> / 4);  // An eighth of a turn right.
  Press(Key::kW);
  Press(Key::kA);  // Forward-left of a view turned right: straight down -Z.
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, -1.0F));
  Release(Key::kA);
  Press(Key::kD);  // Forward-right: straight down +X.
  ExpectNear(input_.Sample().movement.direction, Vec3(1.0F, 0.0F, 0.0F));
}

TEST_F(InputTest, ShiftHeldSprints) {
  Press(Key::kLeftShift);
  EXPECT_TRUE(input_.Sample().movement.sprint);
  Release(Key::kLeftShift);
  EXPECT_FALSE(input_.Sample().movement.sprint);
}

TEST_F(InputTest, CtrlHeldCrouchesAndZHeldGoesProneWinningOverCtrl) {
  Press(Key::kLeftControl);
  EXPECT_EQ(input_.Sample().movement.desired_stance, Stance::kCrouching);
  Press(Key::kZ);
  EXPECT_EQ(input_.Sample().movement.desired_stance, Stance::kProne);
  Release(Key::kLeftControl);
  EXPECT_EQ(input_.Sample().movement.desired_stance, Stance::kProne);
  Release(Key::kZ);
  EXPECT_EQ(input_.Sample().movement.desired_stance, Stance::kStanding);
}

TEST_F(InputTest, TheFirstCursorPositionTurnsNothing) {
  input_.OnMouseMoveEvent(MouseMoveEvent{.x = 640.0F, .y = 360.0F});
  const Command command = input_.Sample();
  EXPECT_FLOAT_EQ(command.yaw, 0.0F);
  EXPECT_FLOAT_EQ(command.pitch, 0.0F);
}

TEST_F(InputTest, MovingTheMouseRightTurnsRightAndUpLooksUpAtTheSensitivity) {
  MoveMouse(0.0F, 0.0F);
  MoveMouse(10.0F, -20.0F);
  const Command command = input_.Sample();
  EXPECT_NEAR(command.yaw, -10.0F * kSensitivity, kTolerance);
  EXPECT_NEAR(command.pitch, 20.0F * kSensitivity, kTolerance);
}

TEST_F(InputTest, TheViewIsKeptAcrossSamples) {
  MoveMouse(0.0F, 0.0F);
  MoveMouse(-30.0F, 0.0F);
  EXPECT_NEAR(input_.Sample().yaw, 30.0F * kSensitivity, kTolerance);
  EXPECT_NEAR(input_.Sample().yaw, 30.0F * kSensitivity, kTolerance);
}

TEST_F(InputTest, PitchStopsShortOfStraightUpAndStraightDown) {
  MoveMouse(0.0F, 0.0F);
  MoveMouse(0.0F, -100000.0F);
  const float up = input_.Sample().pitch;
  EXPECT_FLOAT_EQ(up, augusta::input::kMaxLookPitch);
  EXPECT_LT(up, std::numbers::pi_v<float> / 2);
  MoveMouse(0.0F, 200000.0F);
  EXPECT_FLOAT_EQ(input_.Sample().pitch, -augusta::input::kMaxLookPitch);
}

TEST_F(InputTest, YawWrapsSoTurningForeverStaysWithinOneTurn) {
  MoveMouse(0.0F, 0.0F);
  for (int i = 0; i < 1000; ++i) {
    Turn(1.0F);
  }
  const float yaw = input_.Sample().yaw;
  EXPECT_GT(yaw, -std::numbers::pi_v<float>);
  EXPECT_LE(yaw, std::numbers::pi_v<float>);
  EXPECT_NEAR(yaw, std::remainder(1000.0F, 2 * std::numbers::pi_v<float>), 1e-3F);
}

TEST(ViewRotationTest, AnUnturnedViewLooksDownMinusZ) {
  ExpectNear(augusta::input::ViewRotation(0.0F, 0.0F) * Vec3(0.0F, 0.0F, -1.0F), Vec3(0.0F, 0.0F, -1.0F));
}

TEST(ViewRotationTest, PositiveYawTurnsLeftAndPositivePitchLooksUp) {
  const float quarter = std::numbers::pi_v<float> / 2;
  ExpectNear(augusta::input::ViewRotation(quarter, 0.0F) * Vec3(0.0F, 0.0F, -1.0F), Vec3(-1.0F, 0.0F, 0.0F));
  const Vec3 looking_up = augusta::input::ViewRotation(0.0F, 0.5F) * Vec3(0.0F, 0.0F, -1.0F);
  ExpectNear(looking_up, Vec3(0.0F, std::sin(0.5F), -std::cos(0.5F)));
}

TEST(ViewRotationTest, PitchTiltsTheViewWithoutChangingWhereItFacesAcrossTheGround) {
  const Vec3 forward = augusta::input::ViewRotation(1.0F, 0.7F) * Vec3(0.0F, 0.0F, -1.0F);
  const Vec3 flat_forward = augusta::input::ViewRotation(1.0F, 0.0F) * Vec3(0.0F, 0.0F, -1.0F);
  ExpectNear(augusta::math::Normalize(Vec3(forward.x, 0.0F, forward.z)), flat_forward);
}

}  // namespace
