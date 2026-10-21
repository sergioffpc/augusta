#include "policy_bridge.h"

#include <chrono>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/logging.h"
#include "augusta/policy.h"
#include "augusta/policy_actions.h"
#include "augusta/tick.h"

namespace augusta::simulation {

namespace {

// How often a failing on_tick is logged: one that fails every tick would
// otherwise write a line a tick (ADR-0029).
constexpr std::chrono::seconds kOnTickWarningInterval{1};

// The Spawn point, from 0, each of a Match's players takes with no Game policy
// for it: in order, starting over after the last.
std::vector<std::size_t> InOrderSpawns(std::size_t players, std::size_t spawn_points) {
  std::vector<std::size_t> indices(players);
  for (std::size_t i = 0; i < players; ++i) {
    indices[i] = i % spawn_points;
  }
  return indices;
}

// The line that logs a call of hook on tick that decided nothing.
std::string UndecidedLine(std::string_view hook, tick::Tick tick, const policy::Undecided& undecided) {
  if (undecided.code == policy::UndecidedCode::kHookFailed) {
    return std::format("subsystem=simulationworld event=policy_hook_failed script={} hook={} tick={} error=\"{}\"",
                       policy::kRulesScriptPath, hook, tick, undecided.description);
  }
  return std::format("subsystem=simulationworld event=policy_decision_refused script={} hook={} tick={} reason=\"{}\"",
                     policy::kRulesScriptPath, hook, tick, undecided.description);
}

}  // namespace

PolicyBridge::PolicyBridge(policy::Policy policy)
    : policy_(std::move(policy)), on_tick_warnings_(kOnTickWarningInterval) {}

bool PolicyBridge::MatchEnded() const { return match_ended_; }

std::optional<policy::PolicyAction> PolicyBridge::OnTick(const policy::MatchView& match, tick::Tick tick) {
  if (match_ended_) {
    return std::nullopt;
  }
  auto action = policy_.OnTick(match, tick);
  if (!action) {
    LOG_WARN_LIMITED(on_tick_warnings_, "{}", UndecidedLine(policy::kOnTick, tick, action.error()));
    return std::nullopt;
  }
  if (action->has_value()) {
    match_ended_ = std::holds_alternative<policy::MatchEnd>(**action);
  }
  return *std::move(action);
}

std::vector<std::size_t> PolicyBridge::AssignSpawns(const policy::MatchView& match, std::size_t spawn_points,
                                                    tick::Tick tick) {
  auto assignment = policy_.AssignSpawns(match, spawn_points);
  if (!assignment) {
    LOG_WARN("{}", UndecidedLine(policy::kAssignSpawns, tick, assignment.error()));
  } else if (assignment->has_value()) {
    return **std::move(assignment);
  }
  return InOrderSpawns(match.players.size(), spawn_points);
}

void PolicyBridge::ForgetMatchEnd() { match_ended_ = false; }

}  // namespace augusta::simulation
