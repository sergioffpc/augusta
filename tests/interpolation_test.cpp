#include "augusta/interpolation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/physics.h"

// Pure: buffered by server time and entity, no real clock or ECS.
namespace {

using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;
using augusta::presentation::RemoteBody;
using augusta::presentation::RemoteInterpolator;
using augusta::presentation::RemotePlayer;
using augusta::presentation::SeenTime;
using augusta::presentation::SeenTimeAt;
using augusta::presentation::ServerClock;

constexpr auto kEntityA = static_cast<augusta::presentation::EntityId>(1);
constexpr auto kEntityB = static_cast<augusta::presentation::EntityId>(2);

BodyState At(float x, Stance stance = Stance::kStanding) {
  return BodyState{.position = Vec3(x, 0.0F, 0.0F), .velocity = Vec3(), .stance = stance};
}

// Asserts interpolator has exactly one buffered entity (kEntityA) and returns its body.
RemoteBody Only(const RemoteInterpolator& interpolator, double sample_time) {
  const std::vector<RemotePlayer> sampled = interpolator.Sample(sample_time);
  EXPECT_EQ(sampled.size(), 1U);
  return sampled.front().body;
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, ASessionWithNoUpdatesIsNotSampled) {
  const RemoteInterpolator interpolator;

  EXPECT_TRUE(interpolator.Sample(0.0F).empty());
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, ASingleUpdateIsShownAsIs) {
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 1.0F, At(5.0F, Stance::kCrouching), 0.0F);

  // Before, at, and long after the one update: nothing to interpolate between.
  for (const float render_time : {0.0F, 1.0F, 100.0F}) {
    const RemoteBody body = Only(interpolator, render_time);
    EXPECT_FLOAT_EQ(body.position.x, 5.0F) << render_time;
    EXPECT_EQ(body.stance, Stance::kCrouching) << render_time;
  }
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, PositionIsLinearlyInterpolatedBetweenTheTwoSurroundingUpdates) {
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 0.0F, At(0.0F), 0.0F);
  interpolator.Record(kEntityA, 1.0F, At(10.0F), 0.0F);

  EXPECT_FLOAT_EQ(Only(interpolator, 0.25F).position.x, 2.5F);
  EXPECT_FLOAT_EQ(Only(interpolator, 0.5F).position.x, 5.0F);
  EXPECT_FLOAT_EQ(Only(interpolator, 0.75F).position.x, 7.5F);
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, StanceSwitchesAtTheMidpointBetweenTheTwoUpdates) {
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 0.0F, At(0.0F, Stance::kStanding), 0.0F);
  interpolator.Record(kEntityA, 1.0F, At(10.0F, Stance::kProne), 0.0F);

  EXPECT_EQ(Only(interpolator, 0.25F).stance, Stance::kStanding);
  EXPECT_EQ(Only(interpolator, 0.75F).stance, Stance::kProne);
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, FacingTurnsBetweenTheTwoSurroundingUpdates) {
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 0.0F, At(0.0F), 0.0F);
  interpolator.Record(kEntityA, 1.0F, At(0.0F), 1.0F);

  EXPECT_FLOAT_EQ(Only(interpolator, 0.25F).yaw, 0.25F);
  EXPECT_FLOAT_EQ(Only(interpolator, 0.75F).yaw, 0.75F);
}

// From just short of a half turn left to just short of one right is a small
// turn through the back, not most of a turn through the front.
// Requirements: NFR-02
TEST(RemoteInterpolatorTest, FacingTurnsTheShorterWayRound) {
  constexpr float kTurn = 2.0F * std::numbers::pi_v<float>;
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 0.0F, At(0.0F), 3.0F);
  interpolator.Record(kEntityA, 1.0F, At(0.0F), -3.0F);

  const float halfway = Only(interpolator, 0.5F).yaw;

  EXPECT_NEAR(std::remainder(halfway - std::numbers::pi_v<float>, kTurn), 0.0F, 1e-5F);
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, ARenderTimeBeforeTheFirstUpdateHoldsAtTheFirst) {
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 1.0F, At(0.0F), 0.0F);
  interpolator.Record(kEntityA, 2.0F, At(10.0F), 0.0F);

  EXPECT_FLOAT_EQ(Only(interpolator, 0.0F).position.x, 0.0F);
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, AGapPastTheNewestUpdateHoldsAtTheNewestRatherThanExtrapolating) {
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 0.0F, At(0.0F), 0.0F);
  interpolator.Record(kEntityA, 1.0F, At(10.0F), 0.0F);

  // No update has arrived since t=1; render_time keeps advancing anyway.
  EXPECT_FLOAT_EQ(Only(interpolator, 1.5F).position.x, 10.0F);
  EXPECT_FLOAT_EQ(Only(interpolator, 50.0F).position.x, 10.0F);
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, SamplingRepeatedlyAtTheSameRenderTimeIsUnaffectedByHowManyTimesItWasSampled) {
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 0.0F, At(0.0F), 0.0F);
  interpolator.Record(kEntityA, 1.0F, At(10.0F), 0.0F);

  const float first = Only(interpolator, 0.5F).position.x;
  for (int i = 0; i < 10; ++i) {
    EXPECT_FLOAT_EQ(Only(interpolator, 0.5F).position.x, first) << i;
  }
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, AnOutOfOrderOrRepeatedUpdateDoesNotMoveInterpolationBackward) {
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 1.0F, At(10.0F), 0.0F);

  // Older than, and equal to, the newest recorded timestamp: both ignored.
  interpolator.Record(kEntityA, 0.5F, At(999.0F), 0.0F);
  interpolator.Record(kEntityA, 1.0F, At(999.0F), 0.0F);

  EXPECT_FLOAT_EQ(Only(interpolator, 1.0F).position.x, 10.0F);
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, EachSessionIsBufferedAndInterpolatedIndependently) {
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 0.0F, At(0.0F), 0.0F);
  interpolator.Record(kEntityA, 1.0F, At(10.0F), 0.0F);
  interpolator.Record(kEntityB, 0.0F, At(0.0F), 0.0F);
  interpolator.Record(kEntityB, 1.0F, At(-20.0F), 0.0F);

  const std::vector<RemotePlayer> sampled = interpolator.Sample(0.5F);

  ASSERT_EQ(sampled.size(), 2U);
  for (const RemotePlayer& player : sampled) {
    if (player.entity == kEntityA) {
      EXPECT_FLOAT_EQ(player.body.position.x, 5.0F);
    } else {
      ASSERT_EQ(player.entity, kEntityB);
      EXPECT_FLOAT_EQ(player.body.position.x, -10.0F);
    }
  }
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, ASessionNoLongerInSyncsCurrentListIsNoLongerSampled) {
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 0.0F, At(0.0F), 0.0F);
  interpolator.Record(kEntityB, 0.0F, At(0.0F), 0.0F);

  const std::array<augusta::presentation::EntityId, 1> still_here{kEntityA};
  interpolator.Sync(still_here);

  const std::vector<RemotePlayer> sampled = interpolator.Sample(0.0F);
  ASSERT_EQ(sampled.size(), 1U);
  EXPECT_EQ(sampled.front().entity, kEntityA);
}

// A remote player walking +1 m along x every server tick, as the server
// reports it: tick n's update puts it at x = n, on the server's timeline at
// n * kTickDuration.
using Seconds = double;
constexpr Seconds kTickDuration = 1.0 / 60.0;
constexpr Seconds kFrameDuration = 1.0 / 144.0;

Seconds ServerTime(int tick) { return static_cast<Seconds>(tick) * kTickDuration; }

// When tick's update reaches the client, relative to its server time. Never
// earlier than the tick before it: the client only ever sees newer ticks.
using Arrivals = std::vector<Seconds>;

Arrivals OnTime(int ticks) {
  Arrivals arrivals;
  for (int tick = 0; tick < ticks; ++tick) {
    arrivals.push_back(ServerTime(tick));
  }
  return arrivals;
}

// Late by 0-70 ms, a different amount each tick (bunching updates up at times),
// still within kInterpolationDelay minus one tick of their server time.
Arrivals Jittered(int ticks) {
  constexpr int kSteps = 8;
  constexpr Seconds kStep = 0.01;
  Arrivals arrivals;
  for (int tick = 0; tick < ticks; ++tick) {
    const Seconds late = static_cast<Seconds>((tick * 7) % kSteps) * kStep;
    const Seconds arrival = ServerTime(tick) + late;
    arrivals.push_back(arrivals.empty() ? arrival : std::max(arrivals.back(), arrival));
  }
  return arrivals;
}

// Renders frames every kFrameDuration from frame_time 0 until the last
// arrival, recording each update as it arrives and sampling
// kInterpolationDelay behind the frame - the timeline the caller keeps.
// Updates of ticks in lost are never delivered. Returns every frame's sample
// point and sampled x.
std::vector<std::pair<Seconds, float>> Play(const Arrivals& arrivals, std::span<const int> lost = {}) {
  RemoteInterpolator interpolator;
  std::vector<std::pair<Seconds, float>> samples;
  std::size_t next = 0;
  for (int frame = 0;; ++frame) {
    const Seconds frame_time = static_cast<Seconds>(frame) * kFrameDuration;
    for (; next < arrivals.size() && arrivals[next] <= frame_time; ++next) {
      const int tick = static_cast<int>(next);
      if (std::ranges::find(lost, tick) == lost.end()) {
        interpolator.Record(kEntityA, ServerTime(tick), At(static_cast<float>(tick)), 0.0F);
      }
    }
    if (next == arrivals.size()) {
      return samples;
    }
    const Seconds sample_time = frame_time - augusta::presentation::kInterpolationDelay;
    const std::vector<RemotePlayer> sampled = interpolator.Sample(sample_time);
    if (!sampled.empty()) {
      samples.emplace_back(sample_time, sampled.front().body.position.x);
    }
  }
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, UpdatesOneATickSampledAtFrameTimesAreShownBetweenTheSurroundingUpdates) {
  constexpr int kTicks = 60;

  for (const auto& [sample_time, x] : Play(OnTime(kTicks))) {
    if (sample_time <= 0.0) {
      continue;  // Before the first update: held there.
    }
    // x = tick, so the body sits sample_time / kTickDuration ticks along.
    const double expected = sample_time / kTickDuration;
    EXPECT_NEAR(x, expected, 1e-3) << sample_time;
    if (std::abs(expected - std::round(expected)) > 1e-2) {
      EXPECT_GT(x, std::floor(expected)) << sample_time;
      EXPECT_LT(x, std::ceil(expected)) << sample_time;
    }
  }
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, JitteredArrivalsWithCorrectTicksAreShownAsIfOnTime) {
  constexpr int kTicks = 60;
  const std::vector<std::pair<Seconds, float>> on_time = Play(OnTime(kTicks));
  const std::vector<std::pair<Seconds, float>> jittered = Play(Jittered(kTicks));

  // Jitter delays the last arrival, so the jittered run renders more frames;
  // every frame both rendered shows the same position.
  ASSERT_LE(on_time.size(), jittered.size());
  for (std::size_t frame = 0; frame < on_time.size(); ++frame) {
    EXPECT_FLOAT_EQ(jittered[frame].second, on_time[frame].second) << on_time[frame].first;
  }
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, AGapHoldsAtTheNewestUpdateAndResumedUpdatesContinueWithoutJumpingBackwards) {
  constexpr int kTicks = 60;
  constexpr int kLastBeforeGap = 20;
  constexpr int kFirstAfterGap = 40;
  std::vector<int> lost;
  for (int tick = kLastBeforeGap + 1; tick < kFirstAfterGap; ++tick) {
    lost.push_back(tick);
  }

  const std::vector<std::pair<Seconds, float>> samples = Play(OnTime(kTicks), lost);

  float previous = 0.0F;
  bool held = false;
  for (const auto& [sample_time, x] : samples) {
    EXPECT_GE(x, previous) << sample_time;
    previous = x;
    // Past the newest update, until the next arrives: held there, not extrapolated.
    if (sample_time > ServerTime(kLastBeforeGap) &&
        sample_time + augusta::presentation::kInterpolationDelay < ServerTime(kFirstAfterGap)) {
      EXPECT_FLOAT_EQ(x, static_cast<float>(kLastBeforeGap)) << sample_time;
      held = true;
    }
  }
  EXPECT_TRUE(held);
  EXPECT_GT(previous, static_cast<float>(kFirstAfterGap));
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, KeepsEnoughUpdatesToCoverTheDelayAtTheFastestTickRate) {
  const double fastest_tick_duration = 1.0 / augusta::presentation::kFastestTickRateHz;
  EXPECT_GE(static_cast<double>(augusta::presentation::kUpdatesKept),
            (augusta::presentation::kInterpolationDelay / fastest_tick_duration) + 2.0);
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, BeyondTheUpdatesKeptTheOldestIsDropped) {
  constexpr int kDropped = 5;
  const int ticks = static_cast<int>(augusta::presentation::kUpdatesKept) + kDropped;
  RemoteInterpolator interpolator;
  for (int tick = 0; tick < ticks; ++tick) {
    interpolator.Record(kEntityA, ServerTime(tick), At(static_cast<float>(tick)), 0.0F);
  }

  // Before the oldest kept: held there.
  EXPECT_FLOAT_EQ(Only(interpolator, ServerTime(0)).position.x, static_cast<float>(kDropped));
}

// Requirements: NFR-02
TEST(RemoteInterpolatorTest, SyncWithEveryoneStillPresentKeepsBufferedHistory) {
  RemoteInterpolator interpolator;
  interpolator.Record(kEntityA, 0.0F, At(0.0F), 0.0F);
  interpolator.Record(kEntityA, 1.0F, At(10.0F), 0.0F);

  const std::array<augusta::presentation::EntityId, 1> still_here{kEntityA};
  interpolator.Sync(still_here);

  // The two updates recorded before Sync are still both buffered, so this still
  // interpolates rather than snapping back to a single point.
  EXPECT_FLOAT_EQ(Only(interpolator, 0.5F).position.x, 5.0F);
}

// Requirements: NFR-02
TEST(ServerClockTest, HasNoTimeUntilTheFirstUpdateThenAlignsToIt) {
  ServerClock clock;
  clock.Advance(1.0);
  EXPECT_FALSE(clock.Now().has_value());

  clock.Observe(ServerTime(100));

  EXPECT_DOUBLE_EQ(clock.Now().value(), ServerTime(100));
}

// Requirements: NFR-02
TEST(ServerClockTest, UpdatesInStepLeaveItAdvancingAtRealTime) {
  ServerClock clock;
  clock.Observe(ServerTime(0));
  for (int tick = 1; tick <= 60; ++tick) {
    clock.Advance(kTickDuration);
    clock.Observe(ServerTime(tick));
    EXPECT_NEAR(clock.Now().value(), ServerTime(tick), 1e-9) << tick;
  }
}

// Requirements: NFR-02
TEST(ServerClockTest, ALateOrEarlyUpdateIsCaughtUpWithSlowlyAndNeverBackwards) {
  constexpr Seconds kOffset = 0.05;
  for (const Seconds offset : {kOffset, -kOffset}) {
    ServerClock clock;
    clock.Observe(0.0);
    clock.Observe(offset);
    EXPECT_DOUBLE_EQ(clock.Now().value(), 0.0) << offset;  // Not at once.

    Seconds real_time = 0.0;
    Seconds previous = clock.Now().value();
    for (int frame = 0; frame < 1000; ++frame) {
      clock.Advance(kFrameDuration);
      real_time += kFrameDuration;
      const Seconds now = clock.Now().value();
      const Seconds step = now - previous;
      EXPECT_GE(step, kFrameDuration * (1.0 - ServerClock::kMaxSlew) - 1e-12) << offset;
      EXPECT_LE(step, kFrameDuration * (1.0 + ServerClock::kMaxSlew) + 1e-12) << offset;
      previous = now;
    }
    // Back in step with the server's timeline.
    EXPECT_NEAR(clock.Now().value(), real_time + offset, 1e-9) << offset;
  }
}

// Requirements: NFR-02
TEST(ServerClockTest, AnUpdateTooFarOffTheClockRealignsItAtOnce) {
  ServerClock clock;
  clock.Observe(0.0);
  clock.Advance(1.0);

  clock.Observe(1.0 + (2.0 * ServerClock::kResyncThreshold));

  EXPECT_DOUBLE_EQ(clock.Now().value(), 1.0 + (2.0 * ServerClock::kResyncThreshold));
}

// Requirements: NFR-02
TEST(ServerClockTest, ResetForgetsTheAlignment) {
  ServerClock clock;
  clock.Observe(ServerTime(100));

  clock.Reset();
  EXPECT_FALSE(clock.Now().has_value());

  clock.Observe(ServerTime(5));
  EXPECT_DOUBLE_EQ(clock.Now().value(), ServerTime(5));
}

// The Seen time a Command reports (ADR-0044): which update a frame shows, and how
// far toward the next.
// Requirements: NFR-02
TEST(SeenTimeAtTest, ASampleBetweenTwoTicksIsTheEarlierTickAndHowFarPastIt) {
  const SeenTime seen = SeenTimeAt(ServerTime(100) + (0.25 * kTickDuration), kTickDuration, 90, 110);

  EXPECT_EQ(seen.tick, 100U);
  EXPECT_NEAR(seen.fraction, 0.25F, 1e-4F);
}

// Requirements: NFR-02
TEST(SeenTimeAtTest, TheSeenTimeIsTheMomentTheInterpolatorSamples) {
  RemoteInterpolator interpolator;
  for (int tick = 100; tick <= 102; ++tick) {
    interpolator.Record(kEntityA, ServerTime(tick), At(static_cast<float>(tick)), 0.0F);
  }
  const Seconds sample_time = ServerTime(101) + (0.75 * kTickDuration);

  const SeenTime seen = SeenTimeAt(sample_time, kTickDuration, 100, 102);

  // Shown three quarters of the way from tick 101's update to tick 102's.
  EXPECT_NEAR(Only(interpolator, sample_time).position.x, static_cast<float>(seen.tick) + seen.fraction, 1e-4F);
  EXPECT_EQ(seen.tick, 101U);
}

// Before the first update there is, or past the last, a frame shows that
// update itself: no Seen time is of a moment outside the updates the client holds.
// Requirements: NFR-02
TEST(SeenTimeAtTest, ASampleOutsideTheUpdatesThereAreIsTheNearestOfThem) {
  const SeenTime before = SeenTimeAt(ServerTime(100) - 0.1, kTickDuration, 100, 110);
  const SeenTime past = SeenTimeAt(ServerTime(110) + 0.5, kTickDuration, 100, 110);

  EXPECT_EQ(before.tick, 100U);
  EXPECT_EQ(before.fraction, 0.0F);
  EXPECT_EQ(past.tick, 110U);
  EXPECT_EQ(past.fraction, 0.0F);
}

// Requirements: NFR-02
TEST(SeenTimeAtTest, AFractionIsNeverOutsideZeroToOne) {
  for (int step = 0; step <= 600; ++step) {
    const SeenTime seen = SeenTimeAt(ServerTime(100) + (static_cast<Seconds>(step) * 0.001), kTickDuration, 0, 1000);
    EXPECT_GE(seen.fraction, 0.0F) << step;
    EXPECT_LE(seen.fraction, 1.0F) << step;
  }
}

}  // namespace
