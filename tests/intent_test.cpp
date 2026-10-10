#include "augusta/intent.h"

#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/tick.h"

// The Command an IntentExecutor yields each tick, and how its Intents end,
// from Server views built by hand: no connection and no threads.
namespace {

using augusta::command::Command;
using augusta::harness::AuthoritativeState;
using augusta::harness::EntityId;
using augusta::harness::HoldFire;
using augusta::harness::IntentEnding;
using augusta::harness::IntentExecutor;
using augusta::harness::Look;
using augusta::harness::Move;
using augusta::harness::OnIntentEnded;
using augusta::harness::Reload;
using augusta::harness::ServerView;
using augusta::harness::SessionId;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;

constexpr EntityId kOwn{1};
constexpr augusta::tick::Tick kNewestTick = 100;

// A view of a Match in progress in which this Agent's player is alive.
ServerView InMatch() {
  ServerView view;
  view.accepted = augusta::harness::Admission{
      .session = SessionId{1}, .tick_rate_hz = 60, .parameters = {}, .character = "soldier"};
  view.match_start = augusta::harness::MatchStart{
      .players = {{.session = SessionId{1}, .entity = kOwn, .character = "soldier", .spawn = {}}}, .first_tick = 0};
  view.matches_started = 1;
  view.in_match = true;
  AuthoritativeState state;
  state.tick = kNewestTick;
  state.health = 100.0F;
  state.bodies.push_back({.entity = kOwn, .body = {}, .yaw = 0.0F});
  view.authoritative = state;
  return view;
}

// A view of the Lobby: admitted, no Match under way.
ServerView InLobby() {
  ServerView view = InMatch();
  view.in_match = false;
  view.authoritative.reset();
  return view;
}

Command Next(IntentExecutor& executor, const ServerView& view = InMatch()) {
  return executor.NextCommand(view, BodyState{});
}

// Records every ending it is told of, in order.
struct Endings {
  std::vector<IntentEnding> seen;

  OnIntentEnded Sink() {
    return [this](IntentEnding ending) { seen.push_back(ending); };
  }
};

TEST(IntentExecutorTest, WithNoIntentItHoldsNothing) {
  IntentExecutor executor;

  const Command command = Next(executor);

  EXPECT_EQ(command.movement.direction, Vec3(0.0F));
  EXPECT_FALSE(command.movement.sprint);
  EXPECT_FALSE(command.fire);
  EXPECT_FALSE(command.reload);
  EXPECT_EQ(command.yaw, 0.0F);
  EXPECT_EQ(command.pitch, 0.0F);
}

TEST(IntentExecutorTest, AMoveHoldsItsDirectionSprintAndStanceEveryTick) {
  IntentExecutor executor;
  executor.SetMovement(Move{.direction = Vec3(1.0F, 0.0F, 0.0F), .sprint = true, .stance = Stance::kCrouching});

  for (int tick = 0; tick < 3; ++tick) {
    const Command command = Next(executor);
    EXPECT_EQ(command.movement.direction, Vec3(1.0F, 0.0F, 0.0F));
    EXPECT_TRUE(command.movement.sprint);
    EXPECT_EQ(command.movement.desired_stance, Stance::kCrouching);
  }
}

TEST(IntentExecutorTest, ALookHoldsItsYawAndPitchEveryTick) {
  IntentExecutor executor;
  executor.SetAim(Look{.yaw = 0.5F, .pitch = -0.25F});

  for (int tick = 0; tick < 3; ++tick) {
    const Command command = Next(executor);
    EXPECT_EQ(command.yaw, 0.5F);
    EXPECT_EQ(command.pitch, -0.25F);
  }
}

TEST(IntentExecutorTest, HoldFireHoldsTheTriggerEveryTick) {
  IntentExecutor executor;
  executor.SetTrigger(HoldFire{});

  EXPECT_TRUE(Next(executor).fire);
  EXPECT_TRUE(Next(executor).fire);
}

TEST(IntentExecutorTest, AReloadIsSentOnOneTickAndThenEndsDone) {
  IntentExecutor executor;
  Endings endings;
  executor.SetTrigger(Reload{}, endings.Sink());

  EXPECT_TRUE(Next(executor).reload);
  EXPECT_EQ(endings.seen, std::vector{IntentEnding::kDone});
  EXPECT_FALSE(Next(executor).reload);
  EXPECT_EQ(endings.seen, std::vector{IntentEnding::kDone});
}

// Outside a Match, or once its player is dead, nothing it sends is played: an
// Intent waits, and a Reload is not spent on a tick that cannot use it.
TEST(IntentExecutorTest, OutsideAMatchItHoldsNothingAndItsIntentsWait) {
  IntentExecutor executor;
  Endings endings;
  executor.SetMovement(Move{.direction = Vec3(1.0F, 0.0F, 0.0F), .sprint = false, .stance = Stance::kStanding});
  executor.SetTrigger(Reload{}, endings.Sink());

  const Command lobby = Next(executor, InLobby());

  EXPECT_EQ(lobby.movement.direction, Vec3(0.0F));
  EXPECT_FALSE(lobby.reload);
  EXPECT_TRUE(endings.seen.empty());
  const Command match = Next(executor);
  EXPECT_EQ(match.movement.direction, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_TRUE(match.reload);
}

TEST(IntentExecutorTest, OnceItsPlayerIsDeadItHoldsNothing) {
  IntentExecutor executor;
  executor.SetTrigger(HoldFire{});
  ServerView view = InMatch();
  view.dead = {kOwn};

  EXPECT_FALSE(Next(executor, view).fire);
}

TEST(IntentExecutorTest, EachChannelIsSetWithoutTouchingTheOthers) {
  IntentExecutor executor;
  Endings movement;
  executor.SetMovement(Move{.direction = Vec3(1.0F, 0.0F, 0.0F), .sprint = false, .stance = Stance::kStanding},
                       movement.Sink());
  executor.SetAim(Look{.yaw = 0.5F, .pitch = 0.0F});

  executor.SetTrigger(HoldFire{});
  executor.SetAim(Look{.yaw = 1.0F, .pitch = 0.0F});

  const Command command = Next(executor);
  EXPECT_EQ(command.movement.direction, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(command.yaw, 1.0F);
  EXPECT_TRUE(command.fire);
  EXPECT_TRUE(movement.seen.empty());
}

TEST(IntentExecutorTest, SettingAChannelAgainEndsItsIntentReplacedOnce) {
  IntentExecutor executor;
  Endings first;
  executor.SetTrigger(HoldFire{}, first.Sink());

  executor.SetTrigger(HoldFire{});
  executor.SetTrigger(HoldFire{});

  EXPECT_EQ(first.seen, std::vector{IntentEnding::kReplaced});
}

TEST(IntentExecutorTest, ClearingAChannelEndsItsIntentReplacedAndStopsIt) {
  IntentExecutor executor;
  Endings endings;
  executor.SetMovement(Move{.direction = Vec3(1.0F, 0.0F, 0.0F), .sprint = true, .stance = Stance::kStanding},
                       endings.Sink());
  executor.SetTrigger(HoldFire{});

  executor.ClearMovement();
  executor.ClearTrigger();

  EXPECT_EQ(endings.seen, std::vector{IntentEnding::kReplaced});
  const Command command = Next(executor);
  EXPECT_EQ(command.movement.direction, Vec3(0.0F));
  EXPECT_FALSE(command.movement.sprint);
  EXPECT_FALSE(command.fire);
}

TEST(IntentExecutorTest, AReloadThatIsDoneIsNotReplacedAfterwards) {
  IntentExecutor executor;
  Endings endings;
  executor.SetTrigger(Reload{}, endings.Sink());
  static_cast<void>(Next(executor));

  executor.SetTrigger(HoldFire{});

  EXPECT_EQ(endings.seen, std::vector{IntentEnding::kDone});
}

// A person who lets go of the mouse keeps looking where they were, and one who
// stops walking keeps their stance.
TEST(IntentExecutorTest, WithItsAimOrMovementClearedItKeepsTheLastViewAndStance) {
  IntentExecutor executor;
  executor.SetAim(Look{.yaw = 0.5F, .pitch = 0.25F});
  executor.SetMovement(Move{.direction = Vec3(1.0F, 0.0F, 0.0F), .sprint = false, .stance = Stance::kProne});
  static_cast<void>(Next(executor));

  executor.ClearAim();
  executor.ClearMovement();

  const Command command = Next(executor);
  EXPECT_EQ(command.yaw, 0.5F);
  EXPECT_EQ(command.pitch, 0.25F);
  EXPECT_EQ(command.movement.desired_stance, Stance::kProne);
  EXPECT_EQ(command.movement.direction, Vec3(0.0F));
}

Command EveryField() {
  Command command;
  command.movement = {.direction = Vec3(0.0F, 0.0F, -1.0F), .sprint = true, .desired_stance = Stance::kCrouching};
  command.yaw = 1.5F;
  command.pitch = 0.5F;
  command.ads = true;
  command.fire = true;
  command.reload = true;
  return command;
}

TEST(IntentExecutorTest, ARawHoldsEveryFieldOfItsCommandEveryTick) {
  IntentExecutor executor;
  executor.SetRaw(EveryField());

  for (int tick = 0; tick < 3; ++tick) {
    const Command command = Next(executor);
    EXPECT_EQ(command.movement.direction, Vec3(0.0F, 0.0F, -1.0F));
    EXPECT_TRUE(command.movement.sprint);
    EXPECT_EQ(command.movement.desired_stance, Stance::kCrouching);
    EXPECT_EQ(command.yaw, 1.5F);
    EXPECT_EQ(command.pitch, 0.5F);
    EXPECT_TRUE(command.ads);
    EXPECT_TRUE(command.fire);
  }
}

// Reload is a rising edge (command::Command): a script that forgets to clear
// it does not reload every tick.
TEST(IntentExecutorTest, ARawsReloadIsSentOnExactlyOneTick) {
  IntentExecutor executor;
  executor.SetRaw(EveryField());

  int reloads = 0;
  for (int tick = 0; tick < 5; ++tick) {
    reloads += Next(executor).reload ? 1 : 0;
  }

  EXPECT_EQ(reloads, 1);
}

TEST(IntentExecutorTest, ARawReplacesTheIntentOnEveryChannel) {
  IntentExecutor executor;
  Endings movement;
  Endings aim;
  Endings trigger;
  executor.SetMovement(Move{.direction = Vec3(1.0F, 0.0F, 0.0F), .sprint = false, .stance = Stance::kStanding},
                       movement.Sink());
  executor.SetAim(Look{.yaw = 0.5F, .pitch = 0.0F}, aim.Sink());
  executor.SetTrigger(HoldFire{}, trigger.Sink());

  executor.SetRaw(Command{});

  EXPECT_EQ(movement.seen, std::vector{IntentEnding::kReplaced});
  EXPECT_EQ(aim.seen, std::vector{IntentEnding::kReplaced});
  EXPECT_EQ(trigger.seen, std::vector{IntentEnding::kReplaced});
  const Command command = Next(executor);
  EXPECT_EQ(command.movement.direction, Vec3(0.0F));
  EXPECT_FALSE(command.fire);
}

TEST(IntentExecutorTest, SettingOrClearingAnyChannelEndsARawReplacedAndLeavesTheOthersEmpty) {
  IntentExecutor executor;
  Endings raw;
  executor.SetRaw(EveryField(), raw.Sink());

  executor.SetAim(Look{.yaw = 0.5F, .pitch = 0.0F});
  executor.ClearMovement();

  EXPECT_EQ(raw.seen, std::vector{IntentEnding::kReplaced});
  const Command command = Next(executor);
  EXPECT_EQ(command.yaw, 0.5F);
  EXPECT_EQ(command.movement.direction, Vec3(0.0F));
  EXPECT_FALSE(command.fire);
  EXPECT_FALSE(command.ads);
}

TEST(IntentExecutorTest, ARawSetAgainEndsTheFirstReplacedAndSendsTheNewOnesReload) {
  IntentExecutor executor;
  Endings first;
  executor.SetRaw(EveryField(), first.Sink());
  static_cast<void>(Next(executor));

  executor.SetRaw(EveryField());

  EXPECT_EQ(first.seen, std::vector{IntentEnding::kReplaced});
  EXPECT_TRUE(Next(executor).reload);
}

// The Seen time is how lag compensation judges a round (ADR-0044), not a
// choice of the player's: the newest state's, as this Agent sees the others.
TEST(IntentExecutorTest, TheSeenTimeIsTheNewestStatesEvenWhenARawCarriesAnother) {
  IntentExecutor executor;
  Command raw = EveryField();
  raw.seen_tick = kNewestTick + 50;
  raw.seen_fraction = 0.75F;
  executor.SetRaw(raw);

  const Command command = Next(executor);

  EXPECT_EQ(command.seen_tick, kNewestTick);
  EXPECT_EQ(command.seen_fraction, 0.0F);
}

TEST(IntentExecutorTest, AnEndingMaySetAnotherIntent) {
  IntentExecutor executor;
  executor.SetTrigger(Reload{}, [&executor](IntentEnding /*ending*/) { executor.SetTrigger(HoldFire{}); });

  static_cast<void>(Next(executor));

  EXPECT_TRUE(Next(executor).fire);
}

}  // namespace
