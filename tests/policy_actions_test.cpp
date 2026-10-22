#include "augusta/policy_actions.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <boost/uuid/uuid_io.hpp>
#include <gtest/gtest.h>

#include "answers.h"
#include "augusta/scripting.h"

// The typed contract between Game policy and the server (ADR-0022): what a
// hook returns, as plain data, becomes one of a closed set of actions only once
// C++ has validated it. The hooks themselves are tested through
// simulation::World in policy_test.cpp; these test the validation alone.
namespace {

using augusta::policy::ActionRefusal;
using augusta::policy::MatchEnd;
using augusta::policy::PolicyAction;
using augusta::policy::ReadSpawnAssignment;
using augusta::policy::ReadTickAction;
using augusta::policy::SessionId;
using augusta::policy::SpawnRefusal;
using augusta::scripting::Field;
using augusta::scripting::Value;

// A Session ID, a UUIDv7, told apart by its last byte.
constexpr SessionId Session(std::uint8_t last) {
  return SessionId({0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD4, 0x70, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, last});
}

constexpr SessionId kAlice = Session(1);
constexpr SessionId kBob = Session(2);
const std::vector<SessionId> kBothAlive{kAlice, kBob};

Value Number(double number) { return Value{.data = number}; }

// session as a hook names it: its canonical string.
Value Named(const SessionId& session) { return Value{.data = boost::uuids::to_string(session)}; }

Value Record(std::vector<Field> fields) { return Value{.data = Value::Record(std::move(fields))}; }

Value List(std::vector<Value> values) { return Value{.data = Value::List(std::move(values))}; }

Value Assignment(const SessionId& session, double spawn_point) {
  return Record(
      {Field{.key = "session", .value = Named(session)}, Field{.key = "spawn_point", .value = Number(spawn_point)}});
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
  const auto action = ReadTickAction(Record({Field{.key = "winner", .value = Named(kBob)}}), kBothAlive);

  ASSERT_TRUE(action.has_value());
  EXPECT_EQ(EndOf(*action).winner, kBob);
}

// A hook only compares a Session ID and hands it back, so only the string it
// was given names the player: any other spelling of the same UUID does not.
// Requirements: US-14
TEST(ReadTickActionTest, AWinnerNamedByAnyOtherStringIsRefused) {
  for (const std::string& other :
       {std::string("0190A1B2-C3D4-7000-8000-000000000002"), std::string("{0190a1b2-c3d4-7000-8000-000000000002}"),
        std::string("0190a1b2c3d4700080000000000000002"), std::string("2")}) {
    const auto action = ReadTickAction(Record({Field{.key = "winner", .value = Value{.data = other}}}), kBothAlive);

    ASSERT_FALSE(action.has_value()) << other;
    EXPECT_EQ(action.error(), ActionRefusal::kNotAWinner) << other;
  }
}

// Requirements: US-14
TEST(ReadTickActionTest, ADrawEndsTheMatchWithNoWinner) {
  const auto action = ReadTickAction(Record({Field{.key = "draw", .value = Value{.data = true}}}), kBothAlive);

  ASSERT_TRUE(action.has_value());
  EXPECT_FALSE(EndOf(*action).winner.has_value());
}

// Requirements: US-14
TEST(ReadTickActionTest, AWinnerNotAliveInTheMatchIsRefused) {
  const auto action = ReadTickAction(Record({Field{.key = "winner", .value = Named(kBob)}}), std::vector{kAlice});

  ASSERT_FALSE(action.has_value());
  EXPECT_EQ(action.error(), ActionRefusal::kNotAWinner);
}

// Requirements: US-14
TEST(ReadTickActionTest, TheNilSessionIsNeverAWinner) {
  const auto action =
      ReadTickAction(Record({Field{.key = "winner", .value = Named(SessionId{})}}), std::vector{SessionId{}});

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
      Record({Field{.key = "winner", .value = Number(1)}}),
      Record({Field{.key = "loser", .value = Named(kAlice)}}),
      Record({Field{.key = "draw", .value = Value{.data = true}}, Field{.key = "winner", .value = Named(kAlice)}}),
  };
  for (std::size_t i = 0; i < refused.size(); ++i) {
    const auto action = ReadTickAction(refused[i], kBothAlive);

    ASSERT_FALSE(action.has_value()) << i;
    EXPECT_EQ(action.error(), ActionRefusal::kNotAnAction) << i;
  }
}

// Requirements: US-03
TEST(ReadSpawnAssignmentTest, GivesEachPlayerItsSpawnPointFromZeroInPlayerOrder) {
  const auto spawns = ReadSpawnAssignment(List({Assignment(kBob, 1), Assignment(kAlice, 3)}), kBothAlive, 3);

  ASSERT_TRUE(spawns.has_value());
  EXPECT_EQ(*spawns, (std::vector<std::size_t>{2, 0}));
}

// Requirements: US-03
TEST(ReadSpawnAssignmentTest, TwoPlayersMayShareASpawnPoint) {
  const auto spawns = ReadSpawnAssignment(List({Assignment(kAlice, 1), Assignment(kBob, 1)}), kBothAlive, 1);

  ASSERT_TRUE(spawns.has_value());
  EXPECT_EQ(*spawns, (std::vector<std::size_t>{0, 0}));
}

// Requirements: US-03
TEST(ReadSpawnAssignmentTest, RefusesWhatIsNotAWholeAssignment) {
  const std::vector<std::pair<Value, SpawnRefusal>> refused{
      {Number(1), SpawnRefusal::kNotAList},
      {List({Number(1)}), SpawnRefusal::kNotAnAssignment},
      {List({Record({Field{.key = "session", .value = Named(kAlice)}})}), SpawnRefusal::kNotAnAssignment},
      {List({Assignment(Session(3), 1), Assignment(kBob, 1)}), SpawnRefusal::kUnknownPlayer},
      {List({Record({Field{.key = "session", .value = Number(1)}, Field{.key = "spawn_point", .value = Number(1)}}),
             Assignment(kBob, 1)}),
       SpawnRefusal::kUnknownPlayer},
      {List({Assignment(kAlice, 1), Assignment(kAlice, 2)}), SpawnRefusal::kPlayerTwice},
      {List({Assignment(kAlice, 1)}), SpawnRefusal::kPlayerMissing},
      {List({Assignment(kAlice, 0), Assignment(kBob, 1)}), SpawnRefusal::kNotASpawnPoint},
      {List({Assignment(kAlice, 4), Assignment(kBob, 1)}), SpawnRefusal::kNotASpawnPoint},
      {List({Assignment(kAlice, 1.5), Assignment(kBob, 1)}), SpawnRefusal::kNotASpawnPoint},
      {List({Assignment(kAlice, std::numeric_limits<double>::quiet_NaN()), Assignment(kBob, 1)}),
       SpawnRefusal::kNotASpawnPoint},
  };
  for (std::size_t i = 0; i < refused.size(); ++i) {
    const auto spawns = ReadSpawnAssignment(refused[i].first, kBothAlive, 3);

    ASSERT_FALSE(spawns.has_value()) << i;
    EXPECT_EQ(spawns.error(), refused[i].second) << i;
  }
}

}  // namespace
