#include "augusta/reconciliation.h"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

// The history of commands and predicted states is pure: no physics is stepped
// here, the replay is handed a step of its own.
namespace {

using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::FallState;
using augusta::prediction::History;
using augusta::prediction::kMaxHistory;
using augusta::prediction::Predicted;

Predicted At(float x, float vertical_speed = 0.0F) {
  BodyState body{};
  body.position = Vec3(x, 0.0F, 0.0F);
  return Predicted{.body = body, .fall = FallState{.vertical_speed = vertical_speed, .grounded = false}};
}

// A command that can be told from another by its direction.
augusta::command::Command Command(float x) {
  augusta::command::Command command{};
  command.movement.direction = Vec3(x, 0.0F, 0.0F);
  return command;
}

TEST(HistoryTest, AcknowledgeReturnsTheStatePredictedAfterThatCommand) {
  History history;
  history.Record(1, Command(1.0F), At(1.0F));
  history.Record(2, Command(2.0F), At(2.0F, -3.0F));
  history.Record(3, Command(3.0F), At(3.0F));

  const auto predicted = history.Acknowledge(2);

  ASSERT_TRUE(predicted.has_value());
  EXPECT_EQ(predicted->body.position.x, 2.0F);
  EXPECT_EQ(predicted->fall.vertical_speed, -3.0F);
}

TEST(HistoryTest, AcknowledgeReturnsTheRiflePredictedAfterThatCommand) {
  History history;
  Predicted fired = At(1.0F);
  fired.rifle = {.cooldown = 0.1F, .reload_remaining = 0.0F, .rounds = 14};
  history.Record(1, Command(1.0F), fired);

  const auto predicted = history.Acknowledge(1);

  ASSERT_TRUE(predicted.has_value());
  EXPECT_EQ(predicted->rifle, fired.rifle);
}

TEST(HistoryTest, AcknowledgingDiscardsWhatIsOlderAndTheStateItself) {
  History history;
  for (augusta::command::Sequence sequence = 1; sequence <= 5; ++sequence) {
    history.Record(sequence, Command(1.0F), At(static_cast<float>(sequence)));
  }

  ASSERT_TRUE(history.Acknowledge(3).has_value());

  EXPECT_EQ(history.Size(), 2U);
  EXPECT_FALSE(history.Acknowledge(3).has_value());
  EXPECT_FALSE(history.Acknowledge(2).has_value());
  EXPECT_TRUE(history.Acknowledge(4).has_value());
}

TEST(HistoryTest, ARepeatedAcknowledgementIsStaleAndFindsNothing) {
  History history;
  history.Record(1, Command(1.0F), At(1.0F));
  history.Record(2, Command(2.0F), At(2.0F));
  ASSERT_TRUE(history.Acknowledge(1).has_value());

  EXPECT_FALSE(history.Acknowledge(1).has_value());
  EXPECT_EQ(history.Size(), 1U);
}

TEST(HistoryTest, AnAcknowledgementOlderThanEverythingHeldChangesNothing) {
  History history;
  history.Record(10, Command(1.0F), At(1.0F));
  history.Record(11, Command(2.0F), At(2.0F));

  EXPECT_FALSE(history.Acknowledge(4).has_value());

  EXPECT_EQ(history.Size(), 2U);
}

TEST(HistoryTest, AnAcknowledgementOfACommandNeverRecordedFindsNothing) {
  History history;
  history.Record(1, Command(1.0F), At(1.0F));
  history.Record(3, Command(3.0F), At(3.0F));

  EXPECT_FALSE(history.Acknowledge(2).has_value());
  EXPECT_TRUE(history.Acknowledge(3).has_value());
}

TEST(HistoryTest, ReplayRunsTheStepForEveryCommandHeldOldestFirst) {
  History history;
  history.Record(1, Command(1.0F), At(1.0F));
  history.Record(2, Command(2.0F), At(2.0F));
  history.Record(3, Command(3.0F), At(3.0F));
  ASSERT_TRUE(history.Acknowledge(1).has_value());

  std::vector<float> seen;
  history.Replay([&seen](const augusta::command::Command& command) {
    seen.push_back(command.movement.direction.x);
    return At(command.movement.direction.x * 10.0F);
  });

  EXPECT_EQ(seen, (std::vector<float>{2.0F, 3.0F}));
}

TEST(HistoryTest, ReplayReplacesThePredictedStatesWithWhatTheStepReturned) {
  History history;
  history.Record(1, Command(1.0F), At(1.0F));
  history.Record(2, Command(2.0F), At(2.0F));

  history.Replay(
      [](const augusta::command::Command& command) { return At(command.movement.direction.x * 10.0F, -1.0F); });

  const auto first = history.Acknowledge(1);
  const auto second = history.Acknowledge(2);
  ASSERT_TRUE(first.has_value() && second.has_value());
  EXPECT_EQ(first->body.position.x, 10.0F);
  EXPECT_EQ(second->body.position.x, 20.0F);
  EXPECT_EQ(second->fall.vertical_speed, -1.0F);
}

TEST(HistoryTest, ReplayOfAnEmptyHistoryNeverRunsTheStep) {
  History history;
  int steps = 0;

  history.Replay([&steps](const augusta::command::Command&) {
    ++steps;
    return Predicted{};
  });

  EXPECT_EQ(steps, 0);
}

TEST(HistoryTest, OnlyTheMostRecentStatesAreKept) {
  History history;
  const auto total = static_cast<std::uint32_t>(kMaxHistory + 10);
  for (augusta::command::Sequence sequence = 1; sequence <= total; ++sequence) {
    history.Record(sequence, Command(1.0F), At(0.0F));
  }

  EXPECT_EQ(history.Size(), kMaxHistory);
  EXPECT_FALSE(history.Acknowledge(1).has_value());
}

}  // namespace
