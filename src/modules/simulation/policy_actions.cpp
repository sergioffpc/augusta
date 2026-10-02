#include "augusta/policy_actions.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/scripting.h"

namespace augusta::simulation {

namespace {

// value as a whole number from 1 to max, or nullopt if it is anything else.
std::optional<std::size_t> WholeNumberUpTo(const scripting::Value& value, std::size_t max) {
  const double* number = std::get_if<double>(&value.data);
  // A NaN fails the last test: it equals nothing, not even its own floor.
  if (number == nullptr || *number < 1.0 || *number > static_cast<double>(max) || *number != std::floor(*number)) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*number);
}

// Whether number names session: Session IDs reach a hook as Lua numbers.
bool Names(double number, SessionId session) { return static_cast<double>(std::to_underlying(session)) == number; }

}  // namespace

std::string_view DescribeActionRefusal(ActionRefusal refusal) {
  switch (refusal) {
    case ActionRefusal::kNotAnAction:
      return "it is neither {winner = <Session ID>} nor {draw = true}";
    case ActionRefusal::kNotAWinner:
      return "its winner is not a player alive in the Match";
  }
  std::unreachable();
}

std::expected<std::optional<PolicyAction>, ActionRefusal> ReadTickAction(const scripting::Value& returned,
                                                                         std::span<const SessionId> alive_sessions) {
  if (std::holds_alternative<std::monostate>(returned.data)) {
    return std::nullopt;
  }
  const auto* record = std::get_if<scripting::Value::Record>(&returned.data);
  if (record == nullptr || record->size() != 1) {
    return std::unexpected(ActionRefusal::kNotAnAction);
  }
  const scripting::Field& field = record->front();
  if (const auto* draw = std::get_if<bool>(&field.value.data); field.key == "draw" && draw != nullptr && *draw) {
    return MatchEnd{.winner = std::nullopt};
  }
  const auto* winner = std::get_if<double>(&field.value.data);
  if (field.key != "winner" || winner == nullptr) {
    return std::unexpected(ActionRefusal::kNotAnAction);
  }
  const auto alive = std::ranges::find_if(
      alive_sessions, [&](SessionId session) { return session != SessionId{} && Names(*winner, session); });
  if (alive == alive_sessions.end()) {
    return std::unexpected(ActionRefusal::kNotAWinner);
  }
  return MatchEnd{.winner = *alive};
}

std::string_view DescribeSpawnRefusal(SpawnRefusal refusal) {
  switch (refusal) {
    case SpawnRefusal::kNotAList:
      return "it is not a list";
    case SpawnRefusal::kNotAnAssignment:
      return "an entry is not a record of exactly a session and a spawn_point";
    case SpawnRefusal::kUnknownPlayer:
      return "it names a session that is not a player in the Match";
    case SpawnRefusal::kPlayerTwice:
      return "it names a player twice";
    case SpawnRefusal::kPlayerMissing:
      return "it leaves a player out";
    case SpawnRefusal::kNotASpawnPoint:
      return "a spawn_point is not a whole number from 1 to the number of Spawn points";
  }
  std::unreachable();
}

std::expected<std::vector<std::size_t>, SpawnRefusal> ReadSpawnAssignment(const scripting::Value& answer,
                                                                          std::span<const SessionId> players,
                                                                          std::size_t spawn_points) {
  const auto* list = std::get_if<scripting::Value::List>(&answer.data);
  if (list == nullptr) {
    return std::unexpected(SpawnRefusal::kNotAList);
  }
  std::vector<std::optional<std::size_t>> assigned(players.size());
  for (const scripting::Value& entry : *list) {
    const auto* record = std::get_if<scripting::Value::Record>(&entry.data);
    if (record == nullptr || record->size() != 2 || (*record)[0].key != "session" ||
        (*record)[1].key != "spawn_point") {
      return std::unexpected(SpawnRefusal::kNotAnAssignment);
    }
    const auto* session = std::get_if<double>(&(*record)[0].value.data);
    const auto player = std::ranges::find_if(
        players, [&](SessionId candidate) { return session != nullptr && Names(*session, candidate); });
    if (player == players.end()) {
      return std::unexpected(SpawnRefusal::kUnknownPlayer);
    }
    std::optional<std::size_t>& slot = assigned[static_cast<std::size_t>(player - players.begin())];
    if (slot.has_value()) {
      return std::unexpected(SpawnRefusal::kPlayerTwice);
    }
    const std::optional<std::size_t> spawn_point = WholeNumberUpTo((*record)[1].value, spawn_points);
    if (!spawn_point.has_value()) {
      return std::unexpected(SpawnRefusal::kNotASpawnPoint);
    }
    slot = *spawn_point - 1;
  }
  std::vector<std::size_t> indices;
  indices.reserve(assigned.size());
  for (const std::optional<std::size_t>& slot : assigned) {
    if (!slot.has_value()) {
      return std::unexpected(SpawnRefusal::kPlayerMissing);
    }
    indices.push_back(*slot);
  }
  return indices;
}

}  // namespace augusta::simulation
