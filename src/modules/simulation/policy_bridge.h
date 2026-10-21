#ifndef AUGUSTA_SIMULATION_POLICY_BRIDGE_H_
#define AUGUSTA_SIMULATION_POLICY_BRIDGE_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "augusta/logging.h"
#include "augusta/policy.h"
#include "augusta/policy_actions.h"
#include "augusta/tick.h"

/// \file
/// What SimulationWorld asks of Game policy (ADR-0022), and how it takes an
/// answer that decides nothing: the hook is logged and the world goes on as if
/// it were undefined. No ECS, and private to the simulation module: SimulationWorld
/// builds the Match views it hands over.
namespace augusta::simulation {

/// The Game policy of one SimulationWorld, and what it has decided of the Match
/// the world's players are in.
class PolicyBridge {
 public:
  explicit PolicyBridge(policy::Policy policy);

  /// Whether policy has ended the Match: it then decides no more until
  /// ForgetMatchEnd.
  [[nodiscard]] bool MatchEnded() const;

  /// Calls on_tick with match on tick, unless policy has ended the Match: the
  /// action it takes, if any. A call that fails or decides what it may not is
  /// logged, at most once an interval as it may fail every tick, and decides
  /// nothing.
  std::optional<policy::PolicyAction> OnTick(const policy::MatchView& match, tick::Tick tick);

  /// The Spawn point, from 0 and below spawn_points, each of match's players
  /// takes: what assign_spawns answers, or in order when it is not defined,
  /// answers nil or is refused. A refusal is logged on tick; as the hook is
  /// called once a Match, it is not limited.
  std::vector<std::size_t> AssignSpawns(const policy::MatchView& match, std::size_t spawn_points, tick::Tick tick);

  /// Forgets that policy ended the Match, for the next one.
  void ForgetMatchEnd();

 private:
  policy::Policy policy_;
  logging::Throttle on_tick_warnings_;
  bool match_ended_ = false;
};

}  // namespace augusta::simulation

#endif  // AUGUSTA_SIMULATION_POLICY_BRIDGE_H_
