#include "augusta/policy_actions.h"

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/scripting.h"

// The typed contract between Game policy and the server (ADR-0022): what a
// hook returns, as plain data, becomes one of a closed set of actions only once
// C++ has validated it. The hooks themselves are tested through
// simulation::World in policy_test.cpp; these test the validation alone.
namespace {

using augusta::scripting::Field;
using augusta::scripting::Value;
using augusta::simulation::ActionRefusal;
using augusta::simulation::MatchEnd;
using augusta::simulation::PolicyAction;
using augusta::simulation::ReadSpawnAssignment;
using augusta::simulation::ReadTickAction;
using augusta::simulation::SessionId;
using augusta::simulation::SpawnRefusal;

constexpr SessionId kAlice{1};
constexpr SessionId kBob{2};
const std::vector<SessionId> kBothAlive{kAlice, kBob};

Value Number(double number) { return Value{.data = number}; }

Value Record(std::vector<Field> fields) { return Value{.data = Value::Record(std::move(fields))}; }

Value List(std::vector<Value> values) { return Value{.data = Value::List(std::move(values))}; }

Value Assignment(double session, double spawn_point) {
  return Record(
      {Field{.key = "session", .value = Number(session)}, Field{.key = "spawn_point", .value = Number(spawn_point)}});
}

// The MatchEnd action holds, or a failed test if it is not one.
MatchEnd EndOf(const std::optional<PolicyAction>& action) {
  EXPECT_TRUE(action.has_value());
  EXPECT_TRUE(action.has_value() && std::holds_alternative<MatchEnd>(*action));
  return action.has_value() ? std::get<MatchEnd>(*action) : MatchEnd{};
}

// Requirements: US-14
TEST(ReadTickActionTest, NilTakesNoAction) {
  const auto action = ReadTickAction(Value{}, kBothAlive);

  ASSERT_TRUE(action.has_value());
  EXPECT_FALSE(action->has_value());
}

// Requirements: US-14
TEST(ReadTickActionTest, AWinnerAliveInTheMatchEndsItWithThatWinner) {
  const auto action = ReadTickAction(Record({Field{.key = "winner", .value = Number(2)}}), kBothAlive);

  ASSERT_TRUE(action.has_value());
  EXPECT_EQ(EndOf(*action).winner, kBob);
}

// Requirements: US-14
TEST(ReadTickActionTest, ADrawEndsTheMatchWithNoWinner) {
  const auto action = ReadTickAction(Record({Field{.key = "draw", .value = Value{.data = true}}}), kBothAlive);

  ASSERT_TRUE(action.has_value());
  EXPECT_FALSE(EndOf(*action).winner.has_value());
}

// Requirements: US-14
TEST(ReadTickActionTest, AWinnerNotAliveInTheMatchIsRefused) {
  const auto action = ReadTickAction(Record({Field{.key = "winner", .value = Number(2)}}), std::vector{kAlice});

  ASSERT_FALSE(action.has_value());
  EXPECT_EQ(action.error(), ActionRefusal::kNotAWinner);
}

// Requirements: US-14
TEST(ReadTickActionTest, SessionZeroIsNeverAWinner) {
  const auto action = ReadTickAction(Record({Field{.key = "winner", .value = Number(0)}}), std::vector{SessionId{}});

  ASSERT_FALSE(action.has_value());
  EXPECT_EQ(action.error(), ActionRefusal::kNotAWinner);
}

// Requirements: US-14
TEST(ReadTickActionTest, AnythingElseIsNotAnAction) {
  const std::vector<Value> refused{
      Number(1),
      Value{.data = std::string("winner")},
      List({Number(1)}),
      Record({Field{.key = "draw", .value = Value{.data = false}}}),
      Record({Field{.key = "winner", .value = Value{.data = std::string("1")}}}),
      Record({Field{.key = "loser", .value = Number(1)}}),
      Record({Field{.key = "draw", .value = Value{.data = true}}, Field{.key = "winner", .value = Number(1)}}),
  };
  for (std::size_t i = 0; i < refused.size(); ++i) {
    const auto action = ReadTickAction(refused[i], kBothAlive);

    ASSERT_FALSE(action.has_value()) << i;
    EXPECT_EQ(action.error(), ActionRefusal::kNotAnAction) << i;
  }
}

// Requirements: US-03
TEST(ReadSpawnAssignmentTest, GivesEachPlayerItsSpawnPointFromZeroInPlayerOrder) {
  const auto spawns = ReadSpawnAssignment(List({Assignment(2, 1), Assignment(1, 3)}), kBothAlive, 3);

  ASSERT_TRUE(spawns.has_value());
  EXPECT_EQ(*spawns, (std::vector<std::size_t>{2, 0}));
}

// Requirements: US-03
TEST(ReadSpawnAssignmentTest, TwoPlayersMayShareASpawnPoint) {
  const auto spawns = ReadSpawnAssignment(List({Assignment(1, 1), Assignment(2, 1)}), kBothAlive, 1);

  ASSERT_TRUE(spawns.has_value());
  EXPECT_EQ(*spawns, (std::vector<std::size_t>{0, 0}));
}

// Requirements: US-03
TEST(ReadSpawnAssignmentTest, RefusesWhatIsNotAWholeAssignment) {
  const std::vector<std::pair<Value, SpawnRefusal>> refused{
      {Number(1), SpawnRefusal::kNotAList},
      {List({Number(1)}), SpawnRefusal::kNotAnAssignment},
      {List({Record({Field{.key = "session", .value = Number(1)}})}), SpawnRefusal::kNotAnAssignment},
      {List({Assignment(3, 1), Assignment(2, 1)}), SpawnRefusal::kUnknownPlayer},
      {List({Assignment(1, 1), Assignment(1, 2)}), SpawnRefusal::kPlayerTwice},
      {List({Assignment(1, 1)}), SpawnRefusal::kPlayerMissing},
      {List({Assignment(1, 0), Assignment(2, 1)}), SpawnRefusal::kNotASpawnPoint},
      {List({Assignment(1, 4), Assignment(2, 1)}), SpawnRefusal::kNotASpawnPoint},
      {List({Assignment(1, 1.5), Assignment(2, 1)}), SpawnRefusal::kNotASpawnPoint},
      {List({Assignment(1, std::numeric_limits<double>::quiet_NaN()), Assignment(2, 1)}),
       SpawnRefusal::kNotASpawnPoint},
  };
  for (std::size_t i = 0; i < refused.size(); ++i) {
    const auto spawns = ReadSpawnAssignment(refused[i].first, kBothAlive, 3);

    ASSERT_FALSE(spawns.has_value()) << i;
    EXPECT_EQ(spawns.error(), refused[i].second) << i;
  }
}

}  // namespace
