#include "augusta/reenactment.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/tick.h"

namespace augusta::harness {

namespace {

// The capture's number of the player of view's Match start that spawned at
// spawn, 0 if none of the capture's did.
CapturedPlayer NumberAt(const Script& script, const math::Vec3& spawn) {
  const auto found = std::ranges::find(script.spawns, spawn);
  return found == script.spawns.end() ? CapturedPlayer{0}
                                      : static_cast<CapturedPlayer>(found - script.spawns.begin() + 1);
}

// The Match start player of view that matches, if any.
template <typename Matches>
const MatchPlayer* FindPlayer(const ServerView& view, Matches matches) {
  if (!view.match_start.has_value()) {
    return nullptr;
  }
  const auto found = std::ranges::find_if(view.match_start->players, matches);
  return found == view.match_start->players.end() ? nullptr : &*found;
}

// The capture's number of the player whose body is entity in view's Match.
CapturedPlayer NumberOf(const Script& script, const ServerView& view, EntityId entity) {
  const MatchPlayer* player = FindPlayer(view, [entity](const MatchPlayer& p) { return p.entity == entity; });
  return player == nullptr ? CapturedPlayer{0} : NumberAt(script, player->spawn);
}

// The capture's number of the player of session in view's Match.
CapturedPlayer NumberOf(const Script& script, const ServerView& view, SessionId session) {
  const MatchPlayer* player = FindPlayer(view, [session](const MatchPlayer& p) { return p.session == session; });
  return player == nullptr ? CapturedPlayer{0} : NumberAt(script, player->spawn);
}

// tick as an offset from first, never below 0.
std::uint64_t OffsetOf(tick::Tick tick, tick::Tick first) { return tick > first ? tick - first : 0; }

// The newest server tick view knows of in its Match: the newest State's, or
// until one arrives the Match's first, which the server runs as it sends
// Match start.
tick::Tick NewestTick(const ServerView& view) {
  return view.authoritative.has_value() ? view.authoritative->tick : view.match_start->first_tick;
}

std::string WinnerName(std::optional<CapturedPlayer> winner) {
  return winner.has_value() ? std::to_string(*winner) : std::string("draw");
}

}  // namespace

tick::Tick DueTick(tick::Tick newest_tick, std::uint64_t ahead, tick::Tick round_trip_ticks) {
  return newest_tick + std::max<std::uint64_t>(ahead, round_trip_ticks + 1);
}

Pacer::Pacer(std::vector<CapturedCommand> commands, std::uint8_t tick_rate_hz)
    : commands_(std::move(commands)), max_held_ticks_(command::HeldTicks(tick_rate_hz)) {}

command::Command Pacer::Next(tick::Tick first_tick, tick::Tick due_tick) {
  const std::uint64_t due_offset = OffsetOf(due_tick, first_tick);
  if (next_ >= commands_.size() || commands_[next_].offset > due_offset) {
    return Filler(due_tick);
  }
  const CapturedCommand& captured = commands_[next_++];
  const std::int64_t delay = static_cast<std::int64_t>(captured.offset) - captured.seen_offset;
  last_delay_ = static_cast<tick::Tick>(std::max<std::int64_t>(delay, 0));
  command::Command command = captured.command;
  command.seen_tick = due_tick - std::min(last_delay_, due_tick);
  last_ = command;
  held_ = 0;
  return command;
}

command::Command Pacer::Filler(tick::Tick due_tick) {
  command::Command filler = last_.value_or(command::Command{});
  filler.fire = false;
  filler.reload = false;
  if (held_ < max_held_ticks_ && last_.has_value()) {
    ++held_;
  } else {
    filler.movement.direction = math::Vec3{};
    filler.movement.sprint = false;
  }
  filler.seen_tick = due_tick - std::min(last_delay_, due_tick);
  return filler;
}

Reenactment::Reenactment(Script script) : script_(std::move(script)), pacer_(script_.commands, script_.tick_rate_hz) {}

command::Command Reenactment::NextCommand(const ServerView& view, command::Sequence next_sequence,
                                          tick::Tick round_trip_ticks) {
  if (view.matches_started != 1 || !view.in_match || !view.match_start.has_value()) {
    return command::Command{};
  }
  // Every Command sent this Match and not yet handed to the World is ahead of
  // this one; before the first State, every one sent this Match is.
  const std::uint64_t ahead =
      view.authoritative.has_value()
          ? next_sequence - std::min(view.authoritative->acknowledged_sequence, next_sequence - 1)
          : ticks_in_match_ + 1;
  ++ticks_in_match_;
  return pacer_.Next(view.match_start->first_tick, DueTick(NewestTick(view), ahead, round_trip_ticks));
}

Progress Reenactment::Check(const ServerView& view, tick::Tick round_trip_ticks) const {
  if (view.matches_started == 0) {
    return Progress::kPlaying;
  }
  if (view.matches_started > 1 || !view.in_match || !view.match_start.has_value()) {
    return Progress::kEnded;
  }
  const tick::Tick first = view.match_start->first_tick;
  const tick::Tick newest = NewestTick(view);
  if (script_.end.has_value() && newest >= first + script_.end->offset) {
    return Progress::kEnded;
  }
  // Disconnecting now reaches the server as a Command sent now would, on the
  // tick its body is taken out before.
  if (script_.leave.has_value() && DueTick(newest, 0, round_trip_ticks) >= first + *script_.leave) {
    return Progress::kLeave;
  }
  return Progress::kPlaying;
}

std::vector<std::string> Reenactment::Outcome(const ServerView& view, const std::vector<ObservedDeath>& observed,
                                              tick::Tick last_tick) const {
  const tick::Tick first = view.match_start.has_value() ? view.match_start->first_tick : 0;
  std::vector<std::string> lines;
  std::vector<bool> matched(observed.size(), false);
  for (const CapturedDeath& death : script_.deaths) {
    std::string line =
        std::format("event=death victim={} killer={} captured_offset={}", death.victim, death.killer, death.offset);
    for (std::size_t i = 0; i < observed.size(); ++i) {
      if (!matched[i] && NumberOf(script_, view, observed[i].victim) == death.victim) {
        matched[i] = true;
        line += std::format(" observed_offset={} observed_killer={}", OffsetOf(observed[i].tick, first),
                            NumberOf(script_, view, observed[i].killer));
        break;
      }
    }
    if (line.find("observed_offset") == std::string::npos) {
      line += " observed=none";
    }
    lines.push_back(std::move(line));
  }
  for (std::size_t i = 0; i < observed.size(); ++i) {
    if (!matched[i]) {
      lines.push_back(std::format("event=death victim={} killer={} captured=none observed_offset={}",
                                  NumberOf(script_, view, observed[i].victim),
                                  NumberOf(script_, view, observed[i].killer), OffsetOf(observed[i].tick, first)));
    }
  }
  std::string end = script_.end.has_value() ? std::format("event=match_end captured_offset={} captured_winner={}",
                                                          script_.end->offset, WinnerName(script_.end->winner))
                                            : std::string("event=match_end captured=none");
  if (view.match_end.has_value()) {
    const std::optional<CapturedPlayer> winner =
        view.match_end->winner.transform([&](SessionId session) { return NumberOf(script_, view, session); });
    end += std::format(" observed_offset={} observed_winner={}", OffsetOf(last_tick, first), WinnerName(winner));
  } else {
    end += std::format(" observed=none observed_offset={}", OffsetOf(last_tick, first));
  }
  lines.push_back(std::move(end));
  return lines;
}

std::optional<std::string> Reenactment::PlayerCountMismatch(const Admission& admission) const {
  if (admission.parameters.player_count == script_.spawns.size()) {
    return std::nullopt;
  }
  return std::format("event=player_count_differs server={} capture={}", admission.parameters.player_count,
                     script_.spawns.size());
}

}  // namespace augusta::harness
