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
#include "augusta/math.h"
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

tick::Tick RoundTripTicks(int ping_ms, std::uint8_t tick_rate_hz) {
  constexpr std::uint64_t kMillisecondsPerSecond = 1000;
  if (ping_ms <= 0) {
    return 0;
  }
  const std::uint64_t scaled = static_cast<std::uint64_t>(ping_ms) * tick_rate_hz;
  return (scaled + kMillisecondsPerSecond - 1) / kMillisecondsPerSecond;
}

Pacer::Pacer(std::vector<CapturedCommand> commands, std::uint8_t tick_rate_hz, std::optional<std::uint32_t> leave)
    : commands_(std::move(commands)), max_held_ticks_(command::HeldTicks(tick_rate_hz)), leave_(leave) {}

std::optional<command::Command> Pacer::Next(tick::Tick first_tick, tick::Tick due_tick) {
  const std::uint64_t due_offset = OffsetOf(due_tick, first_tick);
  if (due_offset < slot_ || DoneBeforeLeave()) {
    return std::nullopt;
  }
  slot_ = due_offset + 1;
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

std::vector<PairedDeath> PairDeaths(const Script& script, const ServerView& view,
                                    const std::vector<ObservedDeath>& observed) {
  const tick::Tick first = view.match_start.has_value() ? view.match_start->first_tick : 0;
  std::vector<CapturedDeath> told;
  told.reserve(observed.size());
  for (const ObservedDeath& death : observed) {
    told.push_back(CapturedDeath{.offset = static_cast<std::uint32_t>(OffsetOf(death.tick, first)),
                                 .victim = NumberOf(script, view, death.victim),
                                 .killer = NumberOf(script, view, death.killer)});
  }
  std::vector<bool> taken(told.size(), false);
  std::vector<PairedDeath> pairs;
  for (const CapturedDeath& death : script.deaths) {
    PairedDeath pair{.captured = death, .observed = std::nullopt};
    for (std::size_t i = 0; i < told.size(); ++i) {
      if (!taken[i] && told[i].victim == death.victim) {
        taken[i] = true;
        pair.observed = told[i];
        break;
      }
    }
    pairs.push_back(pair);
  }
  for (std::size_t i = 0; i < told.size(); ++i) {
    if (!taken[i]) {
      pairs.push_back(PairedDeath{.captured = std::nullopt, .observed = told[i]});
    }
  }
  return pairs;
}

Reenactment::Reenactment(Script script)
    : script_(std::move(script)), pacer_(script_.commands, script_.tick_rate_hz, script_.leave) {}

std::optional<command::Command> Reenactment::NextCommand(const ServerView& view, command::Sequence next_sequence,
                                                         tick::Tick round_trip_ticks) {
  if (view.matches_started != 1 || !view.in_match || !view.match_start.has_value()) {
    return command::Command{};
  }
  // Every Command sent this Match and not yet handed to the World is ahead of
  // this one; before the first State, every one sent this Match is.
  const std::uint64_t ahead =
      view.authoritative.has_value()
          ? next_sequence - std::min(view.authoritative->acknowledged_sequence, next_sequence - 1)
          : sent_in_match_ + 1;
  // A round trip's worth are in flight; more than that waiting would only
  // grow the server's queue, at a client that ticks faster than it.
  if (ahead > round_trip_ticks + 1 + kMaxCommandsQueued) {
    return std::nullopt;
  }
  std::optional<command::Command> next =
      pacer_.Next(view.match_start->first_tick, DueTick(NewestTick(view), ahead, round_trip_ticks));
  if (next.has_value()) {
    ++sent_in_match_;
    last_sent_.store(next_sequence);
  }
  done_before_leave_.store(pacer_.DoneBeforeLeave());
  return next;
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
  // Leaving drops whatever the server still holds: only once every Command up
  // to the Leave has gone, and a disconnect sent now reaches the server no
  // sooner than the Leave's tick and a tick after the last of them is due to
  // be handed to the World.
  if (!script_.leave.has_value() || !done_before_leave_.load()) {
    return Progress::kPlaying;
  }
  const command::Sequence acknowledged =
      view.authoritative.has_value() ? view.authoritative->acknowledged_sequence : command::Sequence{0};
  const command::Sequence last_sent = last_sent_.load();
  const tick::Tick arrives = DueTick(newest, 0, round_trip_ticks);
  const bool all_handed = last_sent <= acknowledged || newest + (last_sent - acknowledged) + 1 < arrives;
  return all_handed && arrives >= first + *script_.leave ? Progress::kLeave : Progress::kPlaying;
}

namespace {

// pair's line: the captured Death, then the one told, or why there is none.
std::string DescribeDeath(const PairedDeath& pair, std::optional<std::uint32_t> left_at) {
  const CapturedDeath& named = pair.captured.has_value() ? *pair.captured : *pair.observed;
  std::string line = std::format("event=death victim={} killer={}", named.victim, named.killer);
  if (!pair.captured.has_value()) {
    return line + std::format(" captured=none observed_offset={}", pair.observed->offset);
  }
  line += std::format(" captured_offset={}", pair.captured->offset);
  if (pair.observed.has_value()) {
    return line + std::format(" observed_offset={} observed_killer={}", pair.observed->offset, pair.observed->killer);
  }
  const bool after_leave = left_at.has_value() && pair.captured->offset >= *left_at;
  return line + (after_leave ? " observed=left" : " observed=none");
}

}  // namespace

std::vector<std::string> Reenactment::Outcome(const ServerView& view, const std::vector<ObservedDeath>& observed,
                                              tick::Tick last_tick, Progress how) const {
  const tick::Tick first = view.match_start.has_value() ? view.match_start->first_tick : 0;
  const std::optional<std::uint32_t> left_at = how == Progress::kLeave ? script_.leave : std::nullopt;
  std::vector<std::string> lines;
  for (const PairedDeath& pair : PairDeaths(script_, view, observed)) {
    lines.push_back(DescribeDeath(pair, left_at));
  }
  std::string end = script_.end.has_value() ? std::format("event=match_end captured_offset={} captured_winner={}",
                                                          script_.end->offset, WinnerName(script_.end->winner))
                                            : std::string("event=match_end captured=none");
  if (view.match_end.has_value()) {
    const std::optional<CapturedPlayer> winner =
        view.match_end->winner.transform([&](SessionId session) { return NumberOf(script_, view, session); });
    end += std::format(" observed_offset={} observed_winner={}", OffsetOf(last_tick, first), WinnerName(winner));
  } else {
    end += std::format(" observed={} observed_offset={}", left_at.has_value() ? "left" : "none",
                       OffsetOf(last_tick, first));
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
