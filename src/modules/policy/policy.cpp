#include "augusta/policy.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "answers.h"
#include "augusta/policy_actions.h"
#include "augusta/primitives.h"
#include "augusta/scripting.h"

namespace augusta::policy {
namespace {

scripting::Field NumberField(std::string key, double value) {
  return scripting::Field{.key = std::move(key), .value = {.data = value}};
}

scripting::Field BoolField(std::string key, bool value) {
  return scripting::Field{.key = std::move(key), .value = {.data = value}};
}

scripting::Field StringField(std::string key, std::string value) {
  return scripting::Field{.key = std::move(key), .value = {.data = std::move(value)}};
}

// player as a hook sees it, its fields ordered by key.
scripting::Value ToValue(const PlayerView& player) {
  scripting::Value::Record record{
      BoolField("alive", player.alive),
      StringField("character", player.character),
      NumberField("entity", player.entity),
      NumberField("health", player.health),
      BoolField("killed", player.killer.has_value()),
  };
  if (player.killer.has_value()) {
    record.push_back(NumberField("killer", *player.killer));
  }
  record.push_back(NumberField("session", std::to_underlying(player.session)));
  return scripting::Value{.data = std::move(record)};
}

// The read-only view of the Match every hook is handed: the Player count and
// every player in the Match, ordered by session. Each hook appends what is its
// own (the tick, the number of Spawn points), keeping the keys in order, as
// Value::Record wants.
scripting::Value::Record ToRecord(const MatchView& match) {
  std::vector<const PlayerView*> players;
  players.reserve(match.players.size());
  for (const PlayerView& player : match.players) {
    players.push_back(&player);
  }
  std::ranges::sort(players, {}, [](const PlayerView* player) { return player->session; });
  scripting::Value::List roster;
  roster.reserve(players.size());
  for (const PlayerView* player : players) {
    roster.push_back(ToValue(*player));
  }
  return scripting::Value::Record{
      NumberField("player_count", static_cast<double>(match.player_count)),
      scripting::Field{.key = "players", .value = {.data = std::move(roster)}},
  };
}

std::unexpected<Undecided> HookFailed(const scripting::HookError& error) {
  return std::unexpected(
      Undecided{.code = UndecidedCode::kHookFailed, .description = scripting::DescribeHookError(error)});
}

std::unexpected<Undecided> Refused(std::string_view reason) {
  return std::unexpected(Undecided{.code = UndecidedCode::kRefused, .description = std::string(reason)});
}

}  // namespace

std::string DescribeLoadError(const LoadError& error) {
  return "the policy script " + std::string(kRulesScriptPath) + " failed to load: " + error.message;
}

struct Policy::Impl {
  // Empty when the scenario has no rules.
  std::optional<scripting::Script> rules;

  std::expected<scripting::Value, scripting::HookError> Call(std::string_view hook,
                                                             const scripting::Value::Record& view) {
    if (!rules.has_value()) {
      return scripting::Value{};
    }
    return rules->Call(hook, view);
  }
};

Policy::Policy() : impl_(std::make_unique<Impl>()) {}
Policy::~Policy() = default;
Policy::Policy(Policy&&) noexcept = default;
Policy& Policy::operator=(Policy&&) noexcept = default;

std::expected<Policy, LoadError> Policy::Load(std::string_view rules) {
  auto script = scripting::Script::Load(kRulesScriptPath, rules);
  if (!script) {
    return std::unexpected(LoadError{.message = std::move(script.error().message)});
  }
  Policy policy;
  policy.impl_->rules.emplace(*std::move(script));
  return policy;
}

std::expected<std::optional<PolicyAction>, Undecided> Policy::OnTick(const MatchView& match, primitives::Tick tick) {
  scripting::Value::Record view = ToRecord(match);
  view.push_back(NumberField("tick", static_cast<double>(tick)));
  const auto returned = impl_->Call(kOnTick, view);
  if (!returned) {
    return HookFailed(returned.error());
  }
  std::vector<SessionId> alive;
  for (const PlayerView& player : match.players) {
    if (player.alive) {
      alive.push_back(player.session);
    }
  }
  auto action = ReadTickAction(*returned, alive);
  if (!action) {
    return Refused(DescribeActionRefusal(action.error()));
  }
  return *std::move(action);
}

std::expected<std::optional<std::vector<std::size_t>>, Undecided> Policy::AssignSpawns(const MatchView& match,
                                                                                       std::size_t spawn_points) {
  scripting::Value::Record view = ToRecord(match);
  view.push_back(NumberField("spawn_points", static_cast<double>(spawn_points)));
  const auto answer = impl_->Call(kAssignSpawns, view);
  if (!answer) {
    return HookFailed(answer.error());
  }
  if (std::holds_alternative<std::monostate>(answer->data)) {
    return std::nullopt;
  }
  std::vector<SessionId> sessions;
  sessions.reserve(match.players.size());
  for (const PlayerView& player : match.players) {
    sessions.push_back(player.session);
  }
  auto assignment = ReadSpawnAssignment(*answer, sessions, spawn_points);
  if (!assignment) {
    return Refused(DescribeSpawnRefusal(assignment.error()));
  }
  return *std::move(assignment);
}

}  // namespace augusta::policy
