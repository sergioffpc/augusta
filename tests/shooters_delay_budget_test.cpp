#include <chrono>

#include <gtest/gtest.h>

#include "augusta/interpolation.h"
#include "augusta/simulation.h"

// Pure arithmetic over simulation's cap and presentation's Interpolation delay:
// what a real client's Shooter's delay can reach, held to the cap (ADR-0044).
namespace {

using Seconds = std::chrono::duration<double>;

// ADR-0013's worst-case profile: 75 ms each way.
constexpr Seconds kWorstCaseRoundTrip = std::chrono::milliseconds(150);
// The most ticks a fire Command waits queued on the server, at 60 Hz.
constexpr Seconds kQueuedTicks{2.0 / 60.0};

// A real client shows its target kInterpolationDelay behind the server and
// reports that as its Seen time; its fire Command then takes the round trip and
// its wait in the queue to be judged. On the worst-case link that must still be
// within the cap, so the client is judged where it saw its target.
// Requirements: NFR-02
TEST(ShootersDelayBudgetTest, TheWorstCaseRoundTripInterpolationDelayAndTwoQueuedTicksFitTheCap) {
  const Seconds delay = kWorstCaseRoundTrip + Seconds(augusta::presentation::kInterpolationDelay) + kQueuedTicks;

  EXPECT_LE(delay, Seconds(augusta::simulation::kMaxShootersDelay));
}

}  // namespace
