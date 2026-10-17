#ifndef AUGUSTA_POLICY_ACTIONS_H_
#define AUGUSTA_POLICY_ACTIONS_H_

#include <cstdint>
#include <optional>
#include <variant>

/// \file
/// The typed contract between Game policy and the rest of the server
/// (ADR-0022): a hook returns plain data, and nothing it returns reaches the
/// server until augusta::policy has read it into one of a closed set of
/// actions, validated against what that hook may decide. SimulationWorld hands
/// the server only these, so server::Host acts on typed actions and never
/// interprets policy itself.
namespace augusta::policy {

/// The server's name for the player who controls a body (GLOSSARY.md, "Session
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

}  // namespace augusta::policy

#endif  // AUGUSTA_POLICY_ACTIONS_H_
