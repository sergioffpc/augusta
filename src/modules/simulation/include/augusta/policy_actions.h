#ifndef AUGUSTA_POLICY_ACTIONS_H_
#define AUGUSTA_POLICY_ACTIONS_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "augusta/scripting.h"

/// \file
/// The typed contract between Game policy and the rest of the server
/// (ADR-0022): a hook returns plain data, and nothing it returns reaches the
/// server until it has been read here into one of a closed set of actions,
/// validated against what that hook may decide. SimulationWorld reads every
/// hook's answer through these and hands the server only the result, so
/// server::Host acts on typed actions and never interprets policy itself.
namespace augusta::simulation {

/// The server's name for the player who controls a body (CONTEXT.md, "Session
/// ID"): how Game policy names a player, and a Match end its winner. The
/// server's start at 1; 0 is no player's, and never a winner.
enum class SessionId : std::uint32_t {};

/// Game policy's decision to end the Match (US-14), made by the rules'
/// on_tick on a tick and taken by server::Host after it (ADR-0023).
struct MatchEnd {
  /// The session of the player who won, alive in the Match on the tick it was
  /// decided; nullopt for a draw.
  std::optional<SessionId> winner;

  bool operator==(const MatchEnd&) const = default;
};

/// Every action the rules' on_tick may take. A closed set: it grows only
/// with a decision Game policy is given (ADR-0022).
using PolicyAction = std::variant<MatchEnd>;

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

}  // namespace augusta::simulation

#endif  // AUGUSTA_POLICY_ACTIONS_H_
