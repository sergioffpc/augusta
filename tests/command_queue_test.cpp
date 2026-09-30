#include "command_queue.h"

#include <chrono>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

// The sanity gate and the per-player queue are pure: no socket is opened here.
namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::server::CommandQueue;
using augusta::server::Enqueued;
using augusta::server::HeldTicks;
using augusta::server::kMaxHeldTime;
using augusta::server::kMaxMovementMagnitude;
using augusta::server::kMaxPitch;
using augusta::server::kMaxQueuedCommands;
using augusta::server::Rejection;
using augusta::server::SequencedCommand;
using augusta::server::Validate;

constexpr float kInfinity = std::numeric_limits<float>::infinity();
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// The server's tick rate a test's queue is for, unless the test says otherwise.
constexpr std::uint8_t kTickRate = 60;
const int kMaxHeldTicks = HeldTicks(kTickRate);

SequencedCommand Walk(std::uint32_t sequence, float x = 1.0F) {
  SequencedCommand sequenced{.sequence = sequence};
  sequenced.command.movement.direction = Vec3(x, 0.0F, 0.0F);
  return sequenced;
}

TEST(ValidateTest, AcceptsAWellFormedNewerCommand) {
  EXPECT_TRUE(Validate(Walk(5), 4).has_value());
  EXPECT_TRUE(Validate(Walk(1), 0).has_value());
}

TEST(ValidateTest, RejectsASequenceThatIsNotStrictlyNewer) {
  EXPECT_EQ(Validate(Walk(4), 4).error(), Rejection::kStale);
  EXPECT_EQ(Validate(Walk(3), 4).error(), Rejection::kStale);
  EXPECT_EQ(Validate(Walk(0), 0).error(), Rejection::kStale);
}

TEST(ValidateTest, RejectsNonFiniteNumbersWhereverTheyAre) {
  SequencedCommand command = Walk(1);
  command.command.movement.direction.y = kNaN;
  EXPECT_EQ(Validate(command, 0).error(), Rejection::kNonFinite);

  command = Walk(1, kInfinity);
  EXPECT_EQ(Validate(command, 0).error(), Rejection::kNonFinite);

  command = Walk(1);
  command.command.yaw = kNaN;
  EXPECT_EQ(Validate(command, 0).error(), Rejection::kNonFinite);

  command = Walk(1);
  command.command.pitch = -kInfinity;
  EXPECT_EQ(Validate(command, 0).error(), Rejection::kNonFinite);

  command = Walk(1);
  command.command.view_fraction = kNaN;
  EXPECT_EQ(Validate(command, 0).error(), Rejection::kNonFinite);
}

TEST(ValidateTest, RejectsNumbersOutsideWhatAClientCanProduce) {
  EXPECT_EQ(Validate(Walk(1, kMaxMovementMagnitude + 0.5F), 0).error(), Rejection::kOutOfRange);
  EXPECT_EQ(Validate(Walk(1, 1.0e30F), 0).error(), Rejection::kOutOfRange);

  SequencedCommand command = Walk(1);
  command.command.pitch = kMaxPitch + 0.1F;
  EXPECT_EQ(Validate(command, 0).error(), Rejection::kOutOfRange);
  command.command.pitch = -kMaxPitch - 0.1F;
  EXPECT_EQ(Validate(command, 0).error(), Rejection::kOutOfRange);

  command = Walk(1);
  command.command.yaw = 1.0e9F;
  EXPECT_EQ(Validate(command, 0).error(), Rejection::kOutOfRange);
}

TEST(ValidateTest, AcceptsADiagonalAndALookStraightUp) {
  SequencedCommand command = Walk(1);
  command.command.movement.direction = Vec3(1.0F, 0.0F, 1.0F);
  command.command.pitch = 1.5707F;

  EXPECT_TRUE(Validate(command, 0).has_value());
}

TEST(ValidateTest, EveryRejectionHasADescription) {
  for (const Rejection rejection : {Rejection::kStale, Rejection::kNonFinite, Rejection::kOutOfRange}) {
    EXPECT_FALSE(augusta::server::DescribeRejection(rejection).empty());
  }
}

TEST(CommandQueueTest, HandsOutOneCommandPerTickOldestFirst) {
  CommandQueue queue{kTickRate};
  ASSERT_TRUE(queue.TryEnqueue(Walk(1, 1.0F)).has_value());
  ASSERT_TRUE(queue.TryEnqueue(Walk(2, 0.5F)).has_value());

  const auto first = queue.Next();
  const auto second = queue.Next();

  EXPECT_EQ(first.command.movement.direction.x, 1.0F);
  EXPECT_EQ(first.acknowledged_sequence, 1U);
  EXPECT_EQ(second.command.movement.direction.x, 0.5F);
  EXPECT_EQ(second.acknowledged_sequence, 2U);
}

TEST(CommandQueueTest, SaysHowManyCommandsAreQueuedForTheNextTicks) {
  CommandQueue queue{kTickRate};
  EXPECT_EQ(queue.Queued(), 0U);

  ASSERT_TRUE(queue.TryEnqueue(Walk(1)).has_value());
  ASSERT_TRUE(queue.TryEnqueue(Walk(2)).has_value());
  EXPECT_EQ(queue.Queued(), 2U);

  static_cast<void>(queue.Next());
  EXPECT_EQ(queue.Queued(), 1U);
}

TEST(CommandQueueTest, ARepeatedCommandIsTakenInOnce) {
  CommandQueue queue{kTickRate};
  ASSERT_TRUE(queue.TryEnqueue(Walk(1)).has_value());

  EXPECT_EQ(queue.TryEnqueue(Walk(1)).error(), Rejection::kStale);

  static_cast<void>(queue.Next());
  EXPECT_EQ(queue.Next().acknowledged_sequence, 1U);
}

TEST(CommandQueueTest, ACommandOlderThanOneAlreadyTakenInIsDropped) {
  CommandQueue queue{kTickRate};
  ASSERT_TRUE(queue.TryEnqueue(Walk(5)).has_value());

  EXPECT_EQ(queue.TryEnqueue(Walk(3)).error(), Rejection::kStale);
}

TEST(CommandQueueTest, ADroppedCommandDoesNotAdvanceTheSequenceOrTheAcknowledgement) {
  CommandQueue queue{kTickRate};
  ASSERT_TRUE(queue.TryEnqueue(Walk(1)).has_value());
  SequencedCommand bad = Walk(2);
  bad.command.yaw = kNaN;

  EXPECT_EQ(queue.TryEnqueue(bad).error(), Rejection::kNonFinite);

  // Sequence 2 is still available for a valid command.
  EXPECT_TRUE(queue.TryEnqueue(Walk(2)).has_value());
}

TEST(CommandQueueTest, HoldsTheLastMovementForABoundThenStopsAndKeepsTheStance) {
  CommandQueue queue{kTickRate};
  SequencedCommand crouch = Walk(1);
  crouch.command.movement.sprint = true;
  crouch.command.movement.desired_stance = augusta::physics::Stance::kCrouching;
  ASSERT_TRUE(queue.TryEnqueue(crouch).has_value());
  static_cast<void>(queue.Next());

  for (int i = 0; i < kMaxHeldTicks; ++i) {
    const auto held = queue.Next();
    EXPECT_EQ(held.command.movement.direction.x, 1.0F) << i;
    EXPECT_TRUE(held.command.movement.sprint) << i;
    EXPECT_EQ(held.acknowledged_sequence, 1U);
  }
  const auto stopped = queue.Next();

  EXPECT_EQ(stopped.command.movement.direction, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_FALSE(stopped.command.movement.sprint);
  EXPECT_EQ(stopped.command.movement.desired_stance, augusta::physics::Stance::kCrouching);
  EXPECT_EQ(stopped.acknowledged_sequence, 1U);
}

TEST(CommandQueueTest, ANewCommandRestartsTheHold) {
  CommandQueue queue{kTickRate};
  ASSERT_TRUE(queue.TryEnqueue(Walk(1)).has_value());
  for (int i = 0; i < kMaxHeldTicks + 2; ++i) {
    static_cast<void>(queue.Next());
  }
  ASSERT_TRUE(queue.TryEnqueue(Walk(2)).has_value());
  static_cast<void>(queue.Next());

  EXPECT_EQ(queue.Next().command.movement.direction.x, 1.0F);
}

TEST(CommandQueueTest, HeldAndIdleTicksNeverRepeatAOneShotAction) {
  CommandQueue queue{kTickRate};
  SequencedCommand shoot = Walk(1);
  shoot.command.reload = true;
  shoot.command.fire = true;
  ASSERT_TRUE(queue.TryEnqueue(shoot).has_value());

  const auto real = queue.Next();
  const auto held = queue.Next();

  EXPECT_TRUE(real.command.reload);
  EXPECT_TRUE(real.command.fire);
  EXPECT_FALSE(held.command.reload);
  EXPECT_FALSE(held.command.fire);
}

TEST(CommandQueueTest, WithNothingEverReceivedAPlayerStandsStill) {
  CommandQueue queue{kTickRate};

  const auto idle = queue.Next();

  EXPECT_EQ(idle.command.movement.direction, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_EQ(idle.acknowledged_sequence, 0U);
}

TEST(CommandQueueTest, WhenAClientRunsAheadTheOldestCommandsGo) {
  CommandQueue queue{kTickRate};
  const auto total = static_cast<std::uint32_t>(kMaxQueuedCommands + 4);
  for (std::uint32_t sequence = 1; sequence <= total; ++sequence) {
    ASSERT_TRUE(queue.TryEnqueue(Walk(sequence)).has_value());
  }

  EXPECT_EQ(queue.Next().acknowledged_sequence, 5U);
}

TEST(CommandQueueTest, EnqueuingIntoAFullQueueSaysItDroppedTheOldest) {
  CommandQueue queue{kTickRate};
  const auto full = static_cast<std::uint32_t>(kMaxQueuedCommands);
  for (std::uint32_t sequence = 1; sequence <= full; ++sequence) {
    ASSERT_EQ(queue.TryEnqueue(Walk(sequence)), Enqueued::kQueued) << sequence;
  }

  EXPECT_EQ(queue.TryEnqueue(Walk(full + 1)), Enqueued::kDroppedOldest);
  EXPECT_EQ(queue.Queued(), kMaxQueuedCommands);
}

// How long a queue for a server ticking at tick_rate_hz holds a client's last
// movement once its commands stop.
std::chrono::duration<double> HeldFor(std::uint8_t tick_rate_hz) {
  CommandQueue queue{tick_rate_hz};
  static_cast<void>(queue.TryEnqueue(Walk(1)));
  static_cast<void>(queue.Next());
  int held = 0;
  while (queue.Next().command.movement.direction.x != 0.0F && held < tick_rate_hz) {
    ++held;
  }
  return std::chrono::duration<double>(held / static_cast<double>(tick_rate_hz));
}

TEST(CommandQueueTest, TheLastMovementIsHeldForAboutAHundredMillisecondsAtAnyTickRate) {
  constexpr std::chrono::duration<double> kOneTickAt30Hz{1.0 / 30.0};
  for (const std::uint8_t rate : {std::uint8_t{30}, std::uint8_t{60}}) {
    const std::chrono::duration<double> held = HeldFor(rate);

    EXPECT_GE(held, kMaxHeldTime) << +rate;
    EXPECT_LT(held, kMaxHeldTime + kOneTickAt30Hz) << +rate;
  }
}

}  // namespace
