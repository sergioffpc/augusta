#ifndef AUGUSTA_POLICY_ANSWERS_H_
#define AUGUSTA_POLICY_ANSWERS_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "augusta/policy_actions.h"
#include "augusta/scripting.h"

/// \file
/// How augusta::policy reads a hook's answer, plain data, into what that hook
/// may decide (ADR-0022): validated as it is read, so nothing else reaches the
/// server. Private to the policy module; its tests reach it directly.
namespace augusta::policy {

/// Why a value on_tick returned is not an action it may take.
enum class ActionRefusal : std::uint8_t {
  /// Neither `{winner = id}`, id a Session ID, nor `{draw = true}`.
  kNotAnAction,
  /// A winner that is not the session of a player alive in the Match.
  kNotAWinner,
};

/// refusal as a log line says it (ADR-0029).
[[nodiscard]] std::string_view DescribeActionRefusal(ActionRefusal refusal);

/// What on_tick returned, as the action it takes: nullopt for nil, which takes
/// none; MatchEnd for `{draw = true}`, or for `{winner = id}` with id the
/// session of a player of alive_sessions; a refusal for anything else.
[[nodiscard]] std::expected<std::optional<PolicyAction>, ActionRefusal> ReadTickAction(
    const scripting::Value& returned, std::span<const SessionId> alive_sessions);

/// Why an assign_spawns answer is refused.
enum class SpawnRefusal : std::uint8_t {
  kNotAList,
  /// An entry is not a record of exactly a session and a spawn_point.
  kNotAnAssignment,
  kUnknownPlayer,
  kPlayerTwice,
  kPlayerMissing,
  /// A spawn_point is not a whole number from 1 to the number of Spawn points.
  kNotASpawnPoint,
};

/// refusal as a log line says it (ADR-0029).
[[nodiscard]] std::string_view DescribeSpawnRefusal(SpawnRefusal refusal);

/// The Spawn point, from 0, an assign_spawns answer gives each of players, in
/// their order. The answer is a list holding, for every player once, a record
/// `{session = id, spawn_point = n}`: id its Session ID, and n its Spawn point,
/// from 1 up to spawn_points.
[[nodiscard]] std::expected<std::vector<std::size_t>, SpawnRefusal> ReadSpawnAssignment(
    const scripting::Value& answer, std::span<const SessionId> players, std::size_t spawn_points);

}  // namespace augusta::policy

#endif  // AUGUSTA_POLICY_ANSWERS_H_
