#ifndef AUGUSTA_POLICY_H_
#define AUGUSTA_POLICY_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/policy_actions.h"
#include "augusta/tick.h"
#include "augusta/uuid.h"

/// \file
/// augusta::policy is a scenario's Game policy (ADR-0022): its rules, one Lua
/// script the server reads out of its pack at startup, whose hooks
/// SimulationWorld calls (ADR-0023). It builds the view each hook is handed,
/// calls the hook in the sandboxed Lua embedding (augusta/scripting.h), and
/// reads its answer into a typed Policy action (augusta/policy_actions.h), so
/// its caller never sees Lua or a hook's raw answer. Server-only: game policy
/// is exclusively server-authoritative, never run by either client world.
///
/// A hook the rules do not define, or rules that were never loaded, decide
/// nothing, and the mechanism decides that concern alone. A hook that fails or
/// answers what it may not decide is Undecided, and the next call runs as
/// usual. Deterministic, like the rest of the simulation: the same rules called
/// with the same views decide the same.
namespace augusta::policy {

/// The rules script's path in the scenario's server pack, which is also the
/// name it is logged under (ADR-0022, ADR-0041).
inline constexpr std::string_view kRulesScriptPath = "rules.lua";

/// The hook the Scripts/Behaviours phase calls once a tick while a Match is in
/// progress and policy has not yet ended it.
inline constexpr std::string_view kOnTick = "on_tick";

/// The hook Match start calls once a Match, to place its players.
inline constexpr std::string_view kAssignSpawns = "assign_spawns";

/// One player in the Match as Game policy sees it.
struct PlayerView {
  /// The Entity ID of its body.
  std::uint32_t entity = 0;
  SessionId session = uuid::kNil;
  /// Its Character, by its path in the pack (ADR-0042).
  std::string character;
  bool alive = true;
  float health = 0.0F;
  /// The Entity ID of whoever killed it, only on the tick it was killed.
  std::optional<std::uint32_t> killer;
};

/// What every hook is handed of the Match: the Player count and every player
/// still in it, in any order (a hook sees them ordered by session).
struct MatchView {
  std::size_t player_count = 0;
  std::vector<PlayerView> players;
};

/// Rules that failed to load: Lua's message.
struct LoadError {
  std::string message;
};

/// A message for error fit to log or print, naming the script.
std::string DescribeLoadError(const LoadError& error);

/// Why a hook call decided nothing.
enum class UndecidedCode : std::uint8_t {
  /// The hook raised an error, ran past the instruction limit or returned
  /// something that is not plain data.
  kHookFailed,
  /// It answered with something it may not decide.
  kRefused,
};

/// A hook call that decided nothing: why, and a description fit to log.
struct Undecided {
  UndecidedCode code = UndecidedCode::kHookFailed;
  std::string description;
};

/// The Game policy of one scenario, run on the Simulation thread (ADR-0005).
/// SimulationWorld owns exactly one. Move-only: it owns its Lua state.
class Policy {
 public:
  /// No rules loaded: every hook is undefined.
  Policy();
  ~Policy();
  Policy(const Policy&) = delete;
  Policy& operator=(const Policy&) = delete;
  Policy(Policy&&) noexcept;
  Policy& operator=(Policy&&) noexcept;

  /// Loads the rules script into a sandbox and runs its top level, which
  /// defines its hooks, under the instruction limit. A script that does not
  /// compile, raises an error or reaches for what the sandbox leaves out (io,
  /// os, require, math.random, ...) is the error.
  static std::expected<Policy, LoadError> Load(std::string_view rules);

  /// Calls on_tick with match, as it is on tick: nullopt when it takes no
  /// action, or the action it takes, a MatchEnd whose winner is a player alive
  /// in match.
  std::expected<std::optional<PolicyAction>, Undecided> OnTick(const MatchView& match, tick::Tick tick);

  /// Calls assign_spawns with match and the number of Spawn points: the Spawn
  /// point, from 0 and below spawn_points, it gives each of match's players, in
  /// their order, or nullopt when it does not decide (undefined, or nil).
  std::expected<std::optional<std::vector<std::size_t>>, Undecided> AssignSpawns(const MatchView& match,
                                                                                 std::size_t spawn_points);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::policy

#endif  // AUGUSTA_POLICY_H_
