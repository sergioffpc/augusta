#include "augusta/prediction.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"

// Exercises PredictionWorld's public Tick() surface end-to-end - the local
// entity, its sequenced commands and its reconciliation against what the
// server says - rather than physics::World directly (see physics_test.cpp for
// that) or the pure decision (see reconciliation_test.cpp).
namespace {

using augusta::input::Command;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::StaminaConfig;
using augusta::physics::StaticMesh;
using augusta::prediction::Acknowledgement;
using augusta::prediction::State;
using augusta::prediction::World;

constexpr float kFixedTick = 1.0F / 60.0F;
constexpr int kSettleTicks = 30;
constexpr int kBudgetTicks = 9;  // NFR-02's 150 ms at 60 Hz.

StaticMesh Floor() {
  constexpr float kExtent = 100.0F;
  return StaticMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                               Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                    .indices = {0, 1, 2, 0, 2, 3}};
}

TEST(PredictionWorldTest, TicksTheLocalEntityForwardEachCall) {
  World world{StaminaConfig{}};
  Command command{};
  command.movement.direction = Vec3(1.0F, 0.0F, 0.0F);

  State state;
  for (int i = 0; i < 30; ++i) {
    state = world.Tick(command, 0, std::nullopt, kFixedTick);
  }

  EXPECT_GT(state.local_body.position.x, 0.0F);
}

// A resting player on a floor, ticked with sequences 1, 2, ... and no input.
class ReconciliationTest : public ::testing::Test {
 protected:
  ReconciliationTest() : world_{StaminaConfig{}} {
    EXPECT_TRUE(world_.AddStaticMesh(Floor()).has_value());
    for (int i = 0; i < kSettleTicks; ++i) {
      Tick(std::nullopt);
    }
    rest_ = states_.back();
  }

  State Tick(const std::optional<Acknowledgement>& acknowledgement, const Command& command = Command{}) {
    ++sequence_;
    states_.push_back(world_.Tick(command, sequence_, acknowledgement, kFixedTick).local_body);
    return State{.local_body = states_.back()};
  }

  // What the server would say after command number sequence, if it had the
  // player somewhere other than the rest position.
  Acknowledgement ServerSays(std::uint32_t sequence, const Vec3& offset) const {
    BodyState body = rest_;
    body.position += offset;
    return Acknowledgement{.sequence = sequence, .body = body};
  }

  World world_;
  std::uint32_t sequence_ = 0;
  std::vector<BodyState> states_;
  BodyState rest_{};
};

TEST_F(ReconciliationTest, AnInjectedDivergenceConvergesWithoutOvershootWithinTheBudget) {
  constexpr float kDivergence = 0.5F;
  const float target = rest_.position.x + kDivergence;
  float previous_error = kDivergence;

  // The server answers a command 3 ticks behind the client's newest, as it does at ~50 ms.
  for (int i = 0; i < kBudgetTicks; ++i) {
    const State state = Tick(ServerSays(sequence_ - 2, Vec3(kDivergence, 0.0F, 0.0F)));
    const float error = std::abs(target - state.local_body.position.x);
    EXPECT_LE(error, previous_error + 1e-4F) << "tick " << i;
    EXPECT_LE(state.local_body.position.x, target + 1e-4F) << "overshoot at tick " << i;
    previous_error = error;
  }

  EXPECT_LT(previous_error, 0.05F * kDivergence);
}

TEST_F(ReconciliationTest, ADivergenceBeyondTheSnapDistanceIsCorrectedAtOnce) {
  const State state = Tick(ServerSays(sequence_, Vec3(10.0F, 0.0F, 0.0F)));

  EXPECT_NEAR(state.local_body.position.x, rest_.position.x + 10.0F, 0.05F);
}

TEST_F(ReconciliationTest, TheSameAcknowledgementRepeatedIsActedOnOnce) {
  const Acknowledgement ack = ServerSays(sequence_, Vec3(1.0F, 0.0F, 0.0F));
  const State first = Tick(ack);

  State last = first;
  for (int i = 0; i < 5; ++i) {
    last = Tick(ack);
  }

  // Were it acted on every tick, five more blends would carry it most of the way.
  EXPECT_NEAR(last.local_body.position.x, first.local_body.position.x, 0.01F);
}

TEST_F(ReconciliationTest, AnAcknowledgementForACommandNeverSentChangesNothing) {
  const float before = states_.back().position.x;

  const State state = Tick(ServerSays(sequence_ + 1000, Vec3(1.0F, 0.0F, 0.0F)));

  EXPECT_NEAR(state.local_body.position.x, before, 0.01F);
}

TEST_F(ReconciliationTest, TheServerComparedAtTheCommandItAnswersNotAtTheClientsNewestState) {
  // The client walks; the server, running behind by 5 commands, reports the
  // state the client itself predicted at that command. Comparing it with the
  // newest state instead would pull the client back 5 ticks of walking.
  Command walk{};
  walk.movement.direction = Vec3(1.0F, 0.0F, 0.0F);
  for (int i = 0; i < 30; ++i) {
    const std::uint32_t answered = sequence_ - 4;
    const BodyState predicted_then = states_[answered - 1];
    Tick(Acknowledgement{.sequence = answered, .body = predicted_then}, walk);
  }

  // 30 ticks of walking at 3 m/s.
  EXPECT_NEAR(states_.back().position.x - rest_.position.x, 1.5F, 0.3F);
}

}  // namespace
