#include "augusta/intent.h"

#include <cmath>
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
using augusta::command::ViewDirection;
using augusta::harness::AimAt;
using augusta::harness::AuthoritativeState;
using augusta::harness::EntityId;
using augusta::harness::FireBursts;
using augusta::harness::HoldFire;
using augusta::harness::IntentEnding;
using augusta::harness::IntentExecutor;
using augusta::harness::Look;
using augusta::harness::Move;
using augusta::harness::MoveTo;
using augusta::harness::OnIntentEnded;
using augusta::harness::Reload;
using augusta::harness::ServerView;
using augusta::harness::SessionId;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;

constexpr EntityId kOwn{1};
constexpr EntityId kTarget{2};
constexpr augusta::tick::Tick kNewestTick = 100;
constexpr int kTickRate = 60;

// A view of a Match in progress in which this Agent's player is alive, with a
// full magazine.
ServerView InMatch() {
  ServerView view;
  view.accepted = augusta::harness::Admission{
      .session = SessionId{1}, .tick_rate_hz = kTickRate, .parameters = {}, .character = "soldier"};
  view.match_start = augusta::harness::MatchStart{
      .players = {{.session = SessionId{1}, .entity = kOwn, .character = "soldier", .spawn = {}}}, .first_tick = 0};
  view.matches_started = 1;
  view.in_match = true;
  AuthoritativeState state;
  state.tick = kNewestTick;
  state.health = 100.0F;
  state.rifle.rounds = 30;
  state.bodies.push_back({.entity = kOwn, .body = {}, .yaw = 0.0F});
  view.authoritative = state;
  return view;
}

// A view of a Match in progress in which the newest state puts the target's body at target.
ServerView WithTargetAt(Vec3 target) {
  ServerView view = InMatch();
  view.authoritative->bodies.push_back({.entity = kTarget, .body = {.position = target, .velocity = {}}, .yaw = 0.0F});
  return view;
}

// This Agent's body where its prediction leaves it.
BodyState At(Vec3 position) { return {.position = position, .velocity = {}}; }

Vec3 Horizontal(Vec3 v) { return augusta::math::Normalize(Vec3(v.x, 0.0F, v.z)); }

// Where command looks, along the floor.
Vec3 Aim(const Command& command) { return Horizontal(ViewDirection(command.yaw, command.pitch)); }

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

TEST(IntentExecutorTest, AMoveToHeadsStraightForItsPointWithItsSprintAndStance) {
  IntentExecutor executor;
  executor.SetMovement(MoveTo{.point = Vec3(10.0F, 0.0F, 0.0F), .sprint = true, .stance = Stance::kCrouching});

  const Command command = executor.NextCommand(InMatch(), At(Vec3(0.0F, 0.0F, 10.0F)));

  const Vec3 heading = Horizontal(command.movement.direction);
  EXPECT_NEAR(heading.x, std::sqrt(0.5F), 1e-4F);
  EXPECT_NEAR(heading.z, -std::sqrt(0.5F), 1e-4F);
  EXPECT_TRUE(command.movement.sprint);
  EXPECT_EQ(command.movement.desired_stance, Stance::kCrouching);
}

TEST(IntentExecutorTest, AMoveToEndsArrivedAtItsPointAndStopsThere) {
  IntentExecutor executor;
  Endings endings;
  executor.SetMovement(MoveTo{.point = Vec3(1.0F, 0.0F, 0.0F), .sprint = false, .stance = Stance::kStanding},
                       endings.Sink());

  // Its body moved by each Command a tenth of a metre, a tick at a time.
  Vec3 own(0.0F);
  for (int tick = 0; tick < kTickRate && endings.seen.empty(); ++tick) {
    own += executor.NextCommand(InMatch(), At(own)).movement.direction * 0.1F;
  }

  EXPECT_EQ(endings.seen, std::vector{IntentEnding::kArrived});
  EXPECT_NEAR(own.x, 1.0F, 0.5F);
  EXPECT_EQ(executor.NextCommand(InMatch(), At(own)).movement.direction, Vec3(0.0F));
}

TEST(IntentExecutorTest, AMoveToEndsBlockedOnceItsBodyMakesNoProgressForAboutASecond) {
  IntentExecutor executor;
  Endings endings;
  executor.SetMovement(MoveTo{.point = Vec3(10.0F, 0.0F, 0.0F), .sprint = false, .stance = Stance::kStanding},
                       endings.Sink());

  // Against a wall: its body never leaves the origin.
  int ticks = 0;
  for (; ticks < 3 * kTickRate && endings.seen.empty(); ++ticks) {
    static_cast<void>(executor.NextCommand(InMatch(), At(Vec3(0.0F))));
  }

  EXPECT_EQ(endings.seen, std::vector{IntentEnding::kBlocked});
  EXPECT_GE(ticks, kTickRate * 3 / 4);
  EXPECT_LE(ticks, kTickRate * 3 / 2);
  EXPECT_EQ(executor.NextCommand(InMatch(), At(Vec3(0.0F))).movement.direction, Vec3(0.0F));
}

TEST(IntentExecutorTest, AMoveToThatKeepsGainingGroundIsNotBlocked) {
  IntentExecutor executor;
  Endings endings;
  executor.SetMovement(MoveTo{.point = Vec3(100.0F, 0.0F, 0.0F), .sprint = false, .stance = Stance::kStanding},
                       endings.Sink());

  // Slowly, a twentieth of a metre a tick.
  Vec3 own(0.0F);
  for (int tick = 0; tick < 3 * kTickRate; ++tick) {
    own += executor.NextCommand(InMatch(), At(own)).movement.direction * 0.05F;
  }

  EXPECT_TRUE(endings.seen.empty());
}

// As a client aims from where it shows its own player: the newest state's is a
// round trip behind, by however far the player has moved since.
TEST(IntentExecutorTest, AnAimAtLooksFromItsPredictedBodyAtTheTargetWhereTheNewestStatePlacesIt) {
  IntentExecutor executor;
  executor.SetAim(AimAt{.target = kTarget});

  const Command command = executor.NextCommand(WithTargetAt(Vec3(0.0F, 0.0F, -10.0F)), At(Vec3(10.0F, 0.0F, -10.0F)));

  const Vec3 aim = Aim(command);
  EXPECT_NEAR(aim.x, -1.0F, 1e-3F);
  EXPECT_NEAR(aim.z, 0.0F, 1e-3F);
}

TEST(IntentExecutorTest, AnAimAtFollowsATargetThatMovesBetweenTicks) {
  IntentExecutor executor;
  executor.SetAim(AimAt{.target = kTarget});

  const Command ahead = executor.NextCommand(WithTargetAt(Vec3(0.0F, 0.0F, -10.0F)), At(Vec3(0.0F)));
  const Command right = executor.NextCommand(WithTargetAt(Vec3(10.0F, 0.0F, 0.0F)), At(Vec3(0.0F)));

  EXPECT_NEAR(Aim(ahead).z, -1.0F, 1e-3F);
  EXPECT_NEAR(Aim(right).x, 1.0F, 1e-3F);
}

TEST(IntentExecutorTest, AnAimAtLooksUpAtATargetAboveAndDownAtOneBelow) {
  IntentExecutor above;
  IntentExecutor below;
  above.SetAim(AimAt{.target = kTarget});
  below.SetAim(AimAt{.target = kTarget});

  EXPECT_GT(above.NextCommand(WithTargetAt(Vec3(0.0F, 10.0F, -10.0F)), At(Vec3(0.0F))).pitch, 0.0F);
  EXPECT_LT(below.NextCommand(WithTargetAt(Vec3(0.0F, -10.0F, -10.0F)), At(Vec3(0.0F))).pitch, 0.0F);
}

TEST(IntentExecutorTest, AnAimAtEndsTargetGoneWhenItsTargetDiesAndKeepsTheLastView) {
  IntentExecutor executor;
  Endings endings;
  executor.SetAim(AimAt{.target = kTarget}, endings.Sink());
  const Command aimed = executor.NextCommand(WithTargetAt(Vec3(10.0F, 0.0F, 0.0F)), At(Vec3(0.0F)));
  ServerView view = WithTargetAt(Vec3(0.0F, 0.0F, -10.0F));
  view.dead = {kTarget};

  const Command command = executor.NextCommand(view, At(Vec3(0.0F)));

  EXPECT_EQ(endings.seen, std::vector{IntentEnding::kTargetGone});
  EXPECT_EQ(command.yaw, aimed.yaw);
}

// A player who disconnects leaves the Match, and its body is removed.
TEST(IntentExecutorTest, AnAimAtEndsTargetGoneWhenItsTargetLeavesTheMatch) {
  IntentExecutor executor;
  Endings endings;
  executor.SetAim(AimAt{.target = kTarget}, endings.Sink());
  static_cast<void>(executor.NextCommand(WithTargetAt(Vec3(10.0F, 0.0F, 0.0F)), At(Vec3(0.0F))));

  static_cast<void>(executor.NextCommand(InMatch(), At(Vec3(0.0F))));

  EXPECT_EQ(endings.seen, std::vector{IntentEnding::kTargetGone});
}

// A body is named for one Match: the next may give its name to someone else.
// It ends as the Match does, not once this Agent is in the next.
TEST(IntentExecutorTest, AnAimAtEndsTargetGoneOnceItsMatchIsOver) {
  IntentExecutor executor;
  Endings endings;
  executor.SetAim(AimAt{.target = kTarget}, endings.Sink());
  static_cast<void>(executor.NextCommand(WithTargetAt(Vec3(10.0F, 0.0F, 0.0F)), At(Vec3(0.0F))));

  static_cast<void>(executor.NextCommand(InLobby(), At(Vec3(0.0F))));

  EXPECT_EQ(endings.seen, std::vector{IntentEnding::kTargetGone});
}

TEST(IntentExecutorTest, AnAimAtEndsTargetGoneWhenItsTargetDiesAfterThisAgentHas) {
  IntentExecutor executor;
  Endings endings;
  executor.SetAim(AimAt{.target = kTarget}, endings.Sink());
  ServerView view = WithTargetAt(Vec3(10.0F, 0.0F, 0.0F));
  static_cast<void>(executor.NextCommand(view, At(Vec3(0.0F))));
  view.dead = {kOwn};
  static_cast<void>(executor.NextCommand(view, At(Vec3(0.0F))));
  EXPECT_TRUE(endings.seen.empty());

  view.dead = {kOwn, kTarget};
  static_cast<void>(executor.NextCommand(view, At(Vec3(0.0F))));

  EXPECT_EQ(endings.seen, std::vector{IntentEnding::kTargetGone});
}

// Set in the Lobby, it waits for a Match to aim in.
TEST(IntentExecutorTest, AnAimAtSetOutsideAMatchWaitsForOne) {
  IntentExecutor executor;
  Endings endings;
  executor.SetAim(AimAt{.target = kTarget}, endings.Sink());

  static_cast<void>(executor.NextCommand(InLobby(), At(Vec3(0.0F))));
  const Command command = executor.NextCommand(WithTargetAt(Vec3(0.0F, 0.0F, -10.0F)), At(Vec3(0.0F)));

  EXPECT_TRUE(endings.seen.empty());
  EXPECT_NEAR(Aim(command).z, -1.0F, 1e-3F);
}

// Released between Bursts, the Recoil offset recovers as a person's would.
TEST(IntentExecutorTest, FireBurstsHoldsTheTriggerInBurstsAndReleasesItBetween) {
  IntentExecutor executor;
  executor.SetTrigger(FireBursts{});

  constexpr int kTicks = 10 * kTickRate;
  int bursts = 0;
  int ticks_firing = 0;
  bool was_firing = false;
  for (int tick = 0; tick < kTicks; ++tick) {
    const Command command = Next(executor);
    ASSERT_FALSE(command.reload) << tick;
    bursts += command.fire && !was_firing ? 1 : 0;
    ticks_firing += command.fire ? 1 : 0;
    was_firing = command.fire;
  }

  EXPECT_GT(bursts, 1);
  EXPECT_LT(ticks_firing, kTicks / 2);
}

TEST(IntentExecutorTest, FireBurstsReloadsAnEmptyMagazineInsteadOfFiringAndKeepsOn) {
  IntentExecutor executor;
  Endings endings;
  executor.SetTrigger(FireBursts{}, endings.Sink());
  ServerView empty = InMatch();
  empty.authoritative->rifle.rounds = 0;

  const Command command = Next(executor, empty);

  EXPECT_TRUE(command.reload);
  EXPECT_FALSE(command.fire);
  EXPECT_TRUE(endings.seen.empty());
  EXPECT_TRUE(Next(executor).fire);
}

TEST(IntentExecutorTest, FireBurstsDoesNotPressReloadAgainWhileAReloadIsUnderWay) {
  IntentExecutor executor;
  executor.SetTrigger(FireBursts{});
  ServerView reloading = InMatch();
  reloading.authoritative->rifle.rounds = 0;
  reloading.authoritative->rifle.reload_remaining = 1.0F;

  const Command command = Next(executor, reloading);

  EXPECT_FALSE(command.reload);
  EXPECT_FALSE(command.fire);
}

}  // namespace
