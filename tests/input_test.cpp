#include "augusta/input.h"

#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/window_events.h"

// Pure: device events in, one tick's Command out, with no window or GPU.
namespace {

using augusta::command::Command;
using augusta::command::ViewRotation;
using augusta::input::Aim;
using augusta::input::Config;
using augusta::input::Control;
using augusta::input::Input;
using augusta::input::Keymap;
using augusta::math::Vec3;
using augusta::physics::Stance;
using augusta::window::Event;
using augusta::window::Key;
using augusta::window::KeyEvent;
using augusta::window::KeyState;
using augusta::window::MouseMoveEvent;

constexpr float kSensitivity = 0.01F;
constexpr float kTolerance = 1e-5F;

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

// One event, handed to input as the client Runtime hands it a Poll's worth.
void Handle(Input& input, const Event& event) { input.Handle(std::span(&event, 1)); }

class InputTest : public ::testing::Test {
 protected:
  void Press(Key key) { Handle(input_, KeyEvent{.key = key, .state = KeyState::kPressed}); }
  void Release(Key key) { Handle(input_, KeyEvent{.key = key, .state = KeyState::kReleased}); }
  void Click() {
    Press(Key::kMouseLeft);
    Release(Key::kMouseLeft);
  }
  // Moves the cursor by (dx, dy) pixels from where the previous move left it.
  void MoveMouse(float dx, float dy) {
    cursor_x_ += dx;
    cursor_y_ += dy;
    Handle(input_, MouseMoveEvent{.x = cursor_x_, .y = cursor_y_});
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

// Requirements: US-04
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

// Requirements: US-04
TEST_F(InputTest, AHeldKeyKeepsMovingUntilItIsReleased) {
  Press(Key::kW);
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, -1.0F));
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, -1.0F));
  Release(Key::kW);
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, 0.0F));
}

// Requirements: US-04
TEST_F(InputTest, OppositeKeysCancel) {
  Press(Key::kW);
  Press(Key::kS);
  Press(Key::kA);
  Press(Key::kD);
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, 0.0F));
}

// Requirements: US-04
TEST_F(InputTest, ADiagonalIsNoFasterThanAStraightLine) {
  Press(Key::kW);
  Press(Key::kD);
  const Vec3 direction = input_.Sample().movement.direction;
  EXPECT_NEAR(augusta::math::Length(direction), 1.0F, kTolerance);
  ExpectNear(direction, Vec3(std::numbers::sqrt2_v<float> / 2, 0.0F, -std::numbers::sqrt2_v<float> / 2));
}

// Requirements: US-04
TEST_F(InputTest, MovementFollowsWhereTheViewHasTurned) {
  MoveMouse(0.0F, 0.0F);
  Turn(std::numbers::pi_v<float> / 2);  // A quarter turn left: now looking down -X.
  Press(Key::kW);
  ExpectNear(input_.Sample().movement.direction, Vec3(-1.0F, 0.0F, 0.0F));
  Release(Key::kW);
  Press(Key::kD);
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, -1.0F));
}

// Requirements: US-04
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

// Requirements: US-04, US-05
TEST_F(InputTest, ShiftHeldSprints) {
  Press(Key::kLeftShift);
  EXPECT_TRUE(input_.Sample().movement.sprint);
  Release(Key::kLeftShift);
  EXPECT_FALSE(input_.Sample().movement.sprint);
}

// Requirements: US-04
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

TEST_F(InputTest, HandlesAPollsEventsInTheOrderTheyHappened) {
  const std::vector<Event> events{KeyEvent{.key = Key::kW, .state = KeyState::kPressed},
                                  KeyEvent{.key = Key::kW, .state = KeyState::kReleased},
                                  KeyEvent{.key = Key::kS, .state = KeyState::kPressed}};

  input_.Handle(events);

  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, 1.0F));
}

TEST_F(InputTest, TheFirstCursorPositionTurnsNothing) {
  Handle(input_, MouseMoveEvent{.x = 640.0F, .y = 360.0F});
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

TEST_F(InputTest, TheCurrentAimIsTheCommandsUntilTheMouseMovesAgain) {
  MoveMouse(0.0F, 0.0F);
  MoveMouse(-30.0F, 10.0F);
  Press(Key::kMouseRight);
  const Command command = input_.Sample();

  const Aim aim = input_.CurrentAim();

  EXPECT_FLOAT_EQ(aim.yaw, command.yaw);
  EXPECT_FLOAT_EQ(aim.pitch, command.pitch);
  EXPECT_EQ(aim.ads, command.ads);
}

TEST_F(InputTest, TheCurrentAimTurnsWithTheMouseBetweenSamples) {
  MoveMouse(0.0F, 0.0F);
  const Command command = input_.Sample();
  MoveMouse(-30.0F, 10.0F);

  // A frame drawn after the tick turns by the mouse movement since; the tick's Command does not.
  const Aim aim = input_.CurrentAim();
  EXPECT_NEAR(aim.yaw, 30.0F * kSensitivity, kTolerance);
  EXPECT_NEAR(aim.pitch, -10.0F * kSensitivity, kTolerance);
  EXPECT_FLOAT_EQ(command.yaw, 0.0F);
}

// Requirements: US-06
TEST_F(InputTest, TheCurrentAimHoldsAdsWhileItsKeyIsHeldBetweenSamples) {
  (void)input_.Sample();

  Press(Key::kMouseRight);
  EXPECT_TRUE(input_.CurrentAim().ads);
  Release(Key::kMouseRight);
  EXPECT_FALSE(input_.CurrentAim().ads);
}

TEST_F(InputTest, AControlIsHeldBetweenSamplesWhileItsKeyIsDown) {
  (void)input_.Sample();

  Press(Key::kMouseLeft);
  EXPECT_TRUE(input_.IsHeld(Control::kFire));
  EXPECT_FALSE(input_.IsHeld(Control::kAds));
  Release(Key::kMouseLeft);
  EXPECT_FALSE(input_.IsHeld(Control::kFire));
}

TEST_F(InputTest, PitchStopsShortOfStraightUpAndStraightDown) {
  MoveMouse(0.0F, 0.0F);
  MoveMouse(0.0F, -100000.0F);
  const float up = input_.Sample().pitch;
  EXPECT_FLOAT_EQ(up, augusta::command::kMaxLookPitch);
  EXPECT_LT(up, std::numbers::pi_v<float> / 2);
  MoveMouse(0.0F, 200000.0F);
  EXPECT_FLOAT_EQ(input_.Sample().pitch, -augusta::command::kMaxLookPitch);
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

// Requirements: US-20
TEST_F(InputTest, TheCursorStartsCaptured) { EXPECT_TRUE(input_.CursorCaptured()); }

// Requirements: US-20
TEST_F(InputTest, EscapeReleasesTheCursor) {
  Press(Key::kEscape);
  EXPECT_FALSE(input_.CursorCaptured());
  Release(Key::kEscape);
  EXPECT_FALSE(input_.CursorCaptured());
}

// Requirements: US-20
TEST_F(InputTest, WhileTheCursorIsReleasedTheMouseDoesNotTurnTheView) {
  MoveMouse(0.0F, 0.0F);
  Press(Key::kEscape);
  MoveMouse(300.0F, -200.0F);
  const Command command = input_.Sample();
  EXPECT_FLOAT_EQ(command.yaw, 0.0F);
  EXPECT_FLOAT_EQ(command.pitch, 0.0F);
}

// Requirements: US-20
TEST_F(InputTest, AClickWhileReleasedCapturesTheCursorAgain) {
  Press(Key::kEscape);
  Click();
  EXPECT_TRUE(input_.CursorCaptured());
}

// Requirements: US-20
TEST_F(InputTest, AClickWhileCapturedChangesNothing) {
  Click();
  EXPECT_TRUE(input_.CursorCaptured());
}

// Requirements: US-20
TEST_F(InputTest, RecapturingDoesNotTurnTheViewByWhereTheCursorWasLeft) {
  MoveMouse(0.0F, 0.0F);
  Press(Key::kEscape);
  MoveMouse(500.0F, 500.0F);
  Click();
  MoveMouse(1000.0F, 0.0F);  // Where the recaptured cursor first reports: only a baseline.
  MoveMouse(10.0F, 0.0F);
  EXPECT_NEAR(input_.Sample().yaw, -10.0F * kSensitivity, kTolerance);
}

// Requirements: US-20
TEST_F(InputTest, ReleasingTheCursorLetsGoOfEveryHeldKey) {
  Press(Key::kW);
  Press(Key::kLeftShift);
  Press(Key::kEscape);
  const Command command = input_.Sample();
  ExpectNear(command.movement.direction, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_FALSE(command.movement.sprint);
}

// Requirements: US-20
TEST_F(InputTest, WhileTheCursorIsReleasedKeysDoNothing) {
  Press(Key::kEscape);
  Press(Key::kW);
  Press(Key::kZ);
  const Command command = input_.Sample();
  ExpectNear(command.movement.direction, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_EQ(command.movement.desired_stance, Stance::kStanding);
}

// Requirements: US-20
TEST_F(InputTest, AfterRecapturingKeysCountOnceThePlayerPressesThemAgain) {
  Press(Key::kEscape);
  Press(Key::kW);  // Ignored: pressed while released.
  Click();
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, 0.0F));
  Release(Key::kW);
  Press(Key::kW);
  ExpectNear(input_.Sample().movement.direction, Vec3(0.0F, 0.0F, -1.0F));
}

// Requirements: US-20
TEST_F(InputTest, AMouseButtonClickWhileReleasedCapturesTheCursorWhicheverButtonItIs) {
  Press(Key::kEscape);
  Press(Key::kMouseRight);
  EXPECT_TRUE(input_.CursorCaptured());
}

// Requirements: US-20
TEST_F(InputTest, AKeyboardKeyWhileReleasedDoesNotCaptureTheCursor) {
  Press(Key::kEscape);
  Release(Key::kEscape);
  Press(Key::kSpace);
  EXPECT_FALSE(input_.CursorCaptured());
}

TEST_F(InputTest, LettingGoClearsEveryHeldControlAndKeepsTheCursorCaptured) {
  Press(Key::kW);
  Press(Key::kLeftShift);
  Press(Key::kMouseLeft);
  Press(Key::kMouseRight);
  Command command = input_.Sample();  // Control: held, they move, sprint, fire and aim.
  ExpectNear(command.movement.direction, Vec3(0.0F, 0.0F, -1.0F));
  EXPECT_TRUE(command.movement.sprint);
  EXPECT_TRUE(command.fire);
  EXPECT_TRUE(command.ads);
  input_.LetGo();
  command = input_.Sample();
  ExpectNear(command.movement.direction, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_FALSE(command.movement.sprint);
  EXPECT_FALSE(command.fire);
  EXPECT_FALSE(command.ads);
  EXPECT_TRUE(input_.CursorCaptured());
}

TEST_F(InputTest, AfterLettingGoTheNextCursorPositionOnlySetsABaseline) {
  MoveMouse(0.0F, 0.0F);
  MoveMouse(10.0F, 0.0F);  // Control: before letting go, the cursor turns the view.
  EXPECT_NEAR(input_.CurrentAim().yaw, -10.0F * kSensitivity, kTolerance);
  input_.LetGo();
  MoveMouse(500.0F, 0.0F);  // Where the cursor went while Input was not handed its moves.
  MoveMouse(10.0F, 0.0F);
  EXPECT_NEAR(input_.Sample().yaw, -20.0F * kSensitivity, kTolerance);
}

// Requirements: US-20
TEST_F(InputTest, ReleasingTheCursorLeavesWhatEscapeLeaves) {
  Input by_escape{Config{.mouse_sensitivity = kSensitivity}};
  for (Input* input : {&by_escape, &input_}) {
    Handle(*input, KeyEvent{.key = Key::kW, .state = KeyState::kPressed});
    Handle(*input, KeyEvent{.key = Key::kMouseRight, .state = KeyState::kPressed});
    EXPECT_TRUE(input->IsHeld(Control::kMoveForward));  // Control: held before the release.
  }
  Handle(by_escape, KeyEvent{.key = Key::kEscape, .state = KeyState::kPressed});
  input_.ReleaseCursor();
  for (Input* input : {&by_escape, &input_}) {
    EXPECT_FALSE(input->CursorCaptured());
    const Command command = input->Sample();
    ExpectNear(command.movement.direction, Vec3(0.0F, 0.0F, 0.0F));
    EXPECT_FALSE(command.ads);
  }
}

// Requirements: US-07, US-20
TEST_F(InputTest, AfterReleasingTheCursorTheNextClickCapturesItWithoutFiring) {
  input_.ReleaseCursor();
  Press(Key::kW);  // Ignored: pressed while released.
  EXPECT_FALSE(input_.IsHeld(Control::kMoveForward));
  Press(Key::kMouseLeft);  // Taken by the capture.
  EXPECT_TRUE(input_.CursorCaptured());
  EXPECT_FALSE(input_.Sample().fire);
  Release(Key::kMouseLeft);
  Press(Key::kMouseLeft);  // Control: once captured, a click fires.
  EXPECT_TRUE(input_.Sample().fire);
}

// Every control rebound, with prone on a mouse button.
Keymap Rebound() {
  Keymap keymap = augusta::input::kDefaultKeymap;
  keymap[static_cast<std::size_t>(Control::kMoveForward)] = Key::kUp;
  keymap[static_cast<std::size_t>(Control::kMoveBack)] = Key::kDown;
  keymap[static_cast<std::size_t>(Control::kMoveLeft)] = Key::kLeft;
  keymap[static_cast<std::size_t>(Control::kMoveRight)] = Key::kRight;
  keymap[static_cast<std::size_t>(Control::kSprint)] = Key::kSpace;
  keymap[static_cast<std::size_t>(Control::kCrouch)] = Key::kC;
  keymap[static_cast<std::size_t>(Control::kProne)] = Key::kMouseRight;
  keymap[static_cast<std::size_t>(Control::kFire)] = Key::kMouseMiddle;
  keymap[static_cast<std::size_t>(Control::kAds)] = Key::kF;
  keymap[static_cast<std::size_t>(Control::kReload)] = Key::kE;
  return keymap;
}

// Requirements: US-20
TEST(KeymapTest, TheDefaultsAreWasdShiftCtrlZMouseAndR) {
  const Keymap& keymap = augusta::input::kDefaultKeymap;
  EXPECT_EQ(keymap[static_cast<std::size_t>(Control::kMoveForward)], Key::kW);
  EXPECT_EQ(keymap[static_cast<std::size_t>(Control::kMoveBack)], Key::kS);
  EXPECT_EQ(keymap[static_cast<std::size_t>(Control::kMoveLeft)], Key::kA);
  EXPECT_EQ(keymap[static_cast<std::size_t>(Control::kMoveRight)], Key::kD);
  EXPECT_EQ(keymap[static_cast<std::size_t>(Control::kSprint)], Key::kLeftShift);
  EXPECT_EQ(keymap[static_cast<std::size_t>(Control::kCrouch)], Key::kLeftControl);
  EXPECT_EQ(keymap[static_cast<std::size_t>(Control::kProne)], Key::kZ);
  EXPECT_EQ(keymap[static_cast<std::size_t>(Control::kFire)], Key::kMouseLeft);
  EXPECT_EQ(keymap[static_cast<std::size_t>(Control::kAds)], Key::kMouseRight);
  EXPECT_EQ(keymap[static_cast<std::size_t>(Control::kReload)], Key::kR);
}

// Requirements: US-06, US-07
TEST_F(InputTest, TheLeftButtonHeldFiresAndTheRightButtonHeldAims) {
  Press(Key::kMouseLeft);
  Press(Key::kMouseRight);
  Command command = input_.Sample();
  EXPECT_TRUE(command.fire);
  EXPECT_TRUE(command.ads);
  EXPECT_TRUE(input_.Sample().fire);  // Held: still firing next tick.
  Release(Key::kMouseLeft);
  Release(Key::kMouseRight);
  command = input_.Sample();
  EXPECT_FALSE(command.fire);
  EXPECT_FALSE(command.ads);
}

// Requirements: US-08
TEST_F(InputTest, ReloadIsOnlyTheTickAfterItsKeyIsPressed) {
  Press(Key::kR);
  EXPECT_TRUE(input_.Sample().reload);
  EXPECT_FALSE(input_.Sample().reload);  // Still held: no second reload.
  Release(Key::kR);
  EXPECT_FALSE(input_.Sample().reload);
  Press(Key::kR);
  EXPECT_TRUE(input_.Sample().reload);
}

// Requirements: US-08
TEST_F(InputTest, APressAndReleaseBetweenTwoTicksStillReloads) {
  Press(Key::kR);
  Release(Key::kR);
  EXPECT_TRUE(input_.Sample().reload);
}

// Requirements: US-07, US-20
TEST_F(InputTest, TheClickThatRecapturesTheCursorDoesNotFire) {
  Press(Key::kEscape);
  Release(Key::kEscape);
  Press(Key::kMouseLeft);  // Taken by the capture.
  EXPECT_FALSE(input_.Sample().fire);
  Release(Key::kMouseLeft);
  Press(Key::kMouseLeft);
  EXPECT_TRUE(input_.Sample().fire);
}

// Requirements: US-08, US-20
TEST_F(InputTest, AReloadPressedWhileTheCursorIsReleasedIsIgnored) {
  Press(Key::kEscape);
  Press(Key::kR);
  EXPECT_FALSE(input_.Sample().reload);
}

// Requirements: US-20
TEST(KeymapTest, ReboundCombatControlsMoveToTheirNewKeys) {
  Input input(Config{.mouse_sensitivity = kSensitivity, .keymap = Rebound()});
  for (const Key key : {Key::kMouseMiddle, Key::kF, Key::kE}) {
    Handle(input, KeyEvent{.key = key, .state = KeyState::kPressed});
  }
  const Command command = input.Sample();
  EXPECT_TRUE(command.fire);
  EXPECT_TRUE(command.ads);
  EXPECT_TRUE(command.reload);
}

// Requirements: US-20
TEST(KeymapTest, ReboundControlsMoveToTheirNewKeys) {
  Input input(Config{.mouse_sensitivity = kSensitivity, .keymap = Rebound()});
  const auto press = [&](Key key) { Handle(input, KeyEvent{.key = key, .state = KeyState::kPressed}); };
  press(Key::kUp);
  press(Key::kRight);
  press(Key::kSpace);
  press(Key::kMouseRight);
  const Command command = input.Sample();
  ExpectNear(command.movement.direction,
             Vec3(std::numbers::sqrt2_v<float> / 2, 0.0F, -std::numbers::sqrt2_v<float> / 2));
  EXPECT_TRUE(command.movement.sprint);
  EXPECT_EQ(command.movement.desired_stance, Stance::kProne);
}

// Requirements: US-20
TEST(KeymapTest, TheOldKeysOfReboundControlsDoNothing) {
  Input input(Config{.mouse_sensitivity = kSensitivity, .keymap = Rebound()});
  for (const Key key : {Key::kW, Key::kD, Key::kLeftShift, Key::kLeftControl, Key::kZ, Key::kMouseLeft, Key::kR}) {
    Handle(input, KeyEvent{.key = key, .state = KeyState::kPressed});
  }
  const Command command = input.Sample();
  ExpectNear(command.movement.direction, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_FALSE(command.movement.sprint);
  EXPECT_EQ(command.movement.desired_stance, Stance::kStanding);
  EXPECT_FALSE(command.fire);
  EXPECT_FALSE(command.reload);
}

// Requirements: US-20
TEST(KeymapTest, AControlOnAMouseButtonIsHeldUntilTheButtonIsReleased) {
  Input input(Config{.mouse_sensitivity = kSensitivity, .keymap = Rebound()});
  Handle(input, KeyEvent{.key = Key::kMouseRight, .state = KeyState::kPressed});
  EXPECT_EQ(input.Sample().movement.desired_stance, Stance::kProne);
  Handle(input, KeyEvent{.key = Key::kMouseRight, .state = KeyState::kReleased});
  EXPECT_EQ(input.Sample().movement.desired_stance, Stance::kStanding);
}

// Requirements: US-20
TEST(KeymapTest, ReboundOppositeControlsStillCancelAndProneStillWins) {
  Input input(Config{.mouse_sensitivity = kSensitivity, .keymap = Rebound()});
  for (const Key key : {Key::kUp, Key::kDown, Key::kLeft, Key::kRight, Key::kC, Key::kMouseRight}) {
    Handle(input, KeyEvent{.key = key, .state = KeyState::kPressed});
  }
  const Command command = input.Sample();
  ExpectNear(command.movement.direction, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_EQ(command.movement.desired_stance, Stance::kProne);
}

TEST(KeyNameTest, EveryKeyHasANameThatNamesItBack) {
  for (std::size_t i = 0; i < augusta::window::kKeyCount; ++i) {
    const auto key = static_cast<Key>(i);
    const std::string_view name = augusta::input::NameOf(key);
    EXPECT_FALSE(name.empty()) << i;
    EXPECT_EQ(augusta::input::KeyNamed(name), key) << name;
  }
}

TEST(KeyNameTest, KeysAreNamedAsAPlayerWouldWriteThem) {
  EXPECT_EQ(augusta::input::KeyNamed("W"), Key::kW);
  EXPECT_EQ(augusta::input::KeyNamed("7"), Key::k7);
  EXPECT_EQ(augusta::input::KeyNamed("LeftShift"), Key::kLeftShift);
  EXPECT_EQ(augusta::input::KeyNamed("Space"), Key::kSpace);
  EXPECT_EQ(augusta::input::KeyNamed("F12"), Key::kF12);
  EXPECT_EQ(augusta::input::KeyNamed("MouseRight"), Key::kMouseRight);
}

// Requirements: US-20
TEST(KeyNameTest, AnUnknownNameIsNoKey) {
  EXPECT_FALSE(augusta::input::KeyNamed("w").has_value());
  EXPECT_FALSE(augusta::input::KeyNamed("Shift").has_value());
  EXPECT_FALSE(augusta::input::KeyNamed("").has_value());
}

TEST(ControlNameTest, EveryControlHasANameThatNamesItBack) {
  for (std::size_t i = 0; i < augusta::input::kControlCount; ++i) {
    const auto control = static_cast<Control>(i);
    EXPECT_EQ(augusta::input::ControlNamed(augusta::input::NameOf(control)), control) << i;
  }
  EXPECT_EQ(augusta::input::ControlNamed("move_forward"), Control::kMoveForward);
  EXPECT_EQ(augusta::input::ControlNamed("prone"), Control::kProne);
  EXPECT_EQ(augusta::input::ControlNamed("fire"), Control::kFire);
  EXPECT_EQ(augusta::input::ControlNamed("ads"), Control::kAds);
  EXPECT_EQ(augusta::input::ControlNamed("reload"), Control::kReload);
  EXPECT_FALSE(augusta::input::ControlNamed("jump").has_value());
}

TEST(ViewRotationTest, AnUnturnedViewLooksDownMinusZ) {
  ExpectNear(ViewRotation(0.0F, 0.0F) * Vec3(0.0F, 0.0F, -1.0F), Vec3(0.0F, 0.0F, -1.0F));
}

TEST(ViewRotationTest, PositiveYawTurnsLeftAndPositivePitchLooksUp) {
  const float quarter = std::numbers::pi_v<float> / 2;
  ExpectNear(ViewRotation(quarter, 0.0F) * Vec3(0.0F, 0.0F, -1.0F), Vec3(-1.0F, 0.0F, 0.0F));
  const Vec3 looking_up = ViewRotation(0.0F, 0.5F) * Vec3(0.0F, 0.0F, -1.0F);
  ExpectNear(looking_up, Vec3(0.0F, std::sin(0.5F), -std::cos(0.5F)));
}

TEST(ViewRotationTest, PitchTiltsTheViewWithoutChangingWhereItFacesAcrossTheGround) {
  const Vec3 forward = ViewRotation(1.0F, 0.7F) * Vec3(0.0F, 0.0F, -1.0F);
  const Vec3 flat_forward = ViewRotation(1.0F, 0.0F) * Vec3(0.0F, 0.0F, -1.0F);
  ExpectNear(augusta::math::Normalize(Vec3(forward.x, 0.0F, forward.z)), flat_forward);
}

}  // namespace
