#include "augusta/replay.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/capture.h"
#include "augusta/capture_records.h"
#include "augusta/command.h"
#include "augusta/command_queue.h"
#include "augusta/match.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"

namespace augusta::host {

namespace {

// deaths ordered by victim, then killer, so two lists of one tick compare
// whatever order each was made in.
std::vector<capture::Death> Ordered(std::vector<capture::Death> deaths) {
  std::ranges::sort(deaths, [](const capture::Death& a, const capture::Death& b) {
    return a.victim != b.victim ? a.victim < b.victim : a.killer < b.killer;
  });
  return deaths;
}

std::string DescribeDeaths(const std::vector<capture::Death>& deaths) {
  std::string described = "[";
  for (const capture::Death& death : deaths) {
    described += std::format("{}{}<-{}", described.size() > 1 ? " " : "", death.victim, death.killer);
  }
  return described + "]";
}

std::string DescribeEnd(const std::optional<capture::MatchEnd>& end) {
  if (!end.has_value()) {
    return "none";
  }
  return end->winner.has_value() ? std::format("winner:{}", *end->winner) : "draw";
}

// The body of a Replay's player: its number in the capture, which no
// SimulationWorld body of the Replay shares.
simulation::EntityId BodyOf(capture::Player player) { return simulation::EntityId{player}; }

// The capture's last tick: its Match end's, or its last event's when it ends partway.
std::uint32_t LastOffset(const capture::Capture& capture) {
  return capture.events.empty() ? 0 : capture.events.back().offset;
}

// Whether the Leave at index of events, on its tick, was taken after that
// tick rather than before it: the live server takes the bodies of those who
// left an ended Match out after its last tick, so their Leaves follow that
// tick's Commands and Deaths, or else come right before its Match end.
bool LeavesAfterItsTick(const std::vector<capture::AtOffset<capture::Event>>& events, std::size_t index,
                        bool tick_run) {
  if (tick_run) {
    return true;
  }
  const std::uint32_t offset = events[index].offset;
  for (std::size_t i = index; i < events.size() && events[i].offset == offset; ++i) {
    if (std::holds_alternative<capture::MatchEnd>(events[i].event)) {
      return true;
    }
    if (!std::holds_alternative<capture::Leave>(events[i].event)) {
      return false;
    }
  }
  return false;
}

}  // namespace

std::string DescribeDivergence(const Divergence& divergence) {
  if (divergence.marker == Divergence::Marker::kDeath) {
    return std::format("marker=death offset={} captured={} resolved={}", divergence.offset,
                       DescribeDeaths(divergence.captured_deaths), DescribeDeaths(divergence.resolved_deaths));
  }
  return std::format("marker=match_end offset={} captured={} resolved={}", divergence.offset,
                     DescribeEnd(divergence.captured_end), DescribeEnd(divergence.resolved_end));
}

ReplayCheck::ReplayCheck(const capture::Capture& capture) {
  for (const capture::AtOffset<capture::Event>& event : capture.events) {
    if (const auto* death = std::get_if<capture::Death>(&event.event)) {
      deaths_[event.offset].push_back(*death);
    } else if (const auto* end = std::get_if<capture::MatchEnd>(&event.event)) {
      end_offset_ = event.offset;
      end_ = *end;
    }
  }
}

std::optional<Divergence> ReplayCheck::Check(std::uint32_t offset, std::vector<capture::Death> deaths,
                                             const std::optional<capture::MatchEnd>& end) {
  if (reported_) {
    return std::nullopt;
  }
  const auto captured = deaths_.find(offset);
  std::vector<capture::Death> captured_deaths =
      Ordered(captured == deaths_.end() ? std::vector<capture::Death>{} : captured->second);
  std::vector<capture::Death> resolved_deaths = Ordered(std::move(deaths));
  if (captured_deaths != resolved_deaths) {
    reported_ = true;
    return Divergence{.marker = Divergence::Marker::kDeath,
                      .offset = offset,
                      .captured_deaths = std::move(captured_deaths),
                      .resolved_deaths = std::move(resolved_deaths),
                      .captured_end = std::nullopt,
                      .resolved_end = std::nullopt};
  }
  // A capture that ends partway has no Match end to hold the World's to.
  if (!end_offset_.has_value()) {
    return std::nullopt;
  }
  const std::optional<capture::MatchEnd> captured_end =
      *end_offset_ == offset ? std::optional<capture::MatchEnd>(end_) : std::nullopt;
  if (captured_end == end) {
    return std::nullopt;
  }
  reported_ = true;
  return Divergence{.marker = Divergence::Marker::kMatchEnd,
                    .offset = offset,
                    .captured_deaths = {},
                    .resolved_deaths = {},
                    .captured_end = captured_end,
                    .resolved_end = end};
}

Replay::Replay(capture::Capture capture, simulation::World world,
               const std::unordered_map<std::string, simulation::Character>& characters)
    : capture_(std::move(capture)), world_(std::move(world)), check_(capture_), last_offset_(LastOffset(capture_)) {
  const std::uint8_t tick_rate_hz = capture_.header.tick_rate_hz;
  for (std::size_t i = 0; i < capture_.joins.size(); ++i) {
    const capture::Join& join = capture_.joins[i];
    const auto number = static_cast<capture::Player>(i + 1);
    start_.players.push_back(
        MatchPlayer{.session = join.session, .entity = static_cast<EntityId>(number), .character = join.character});
    spawns_.push_back(join.spawn);
    players_.push_back(Player{.number = number, .commands = CommandQueue{tick_rate_hz}});
    // As World::StartMatch adds them, but where the capture has them: Game
    // policy is not asked for spawns (ADR-0051).
    world_.AddPlayer(BodyOf(number), join.spawn, characters.at(join.character),
                     simulation::PlayerIdentity{.session = join.session, .character = join.character});
  }
}

void Replay::HandBefore(std::uint32_t offset) {
  const std::vector<capture::AtOffset<capture::Event>>& events = capture_.events;
  bool tick_run = false;
  for (; next_event_ < events.size() && events[next_event_].offset == offset; ++next_event_) {
    const capture::AtOffset<capture::Event>& event = events[next_event_];
    if (const auto* command = std::get_if<capture::Command>(&event.event)) {
      tick_run = true;
      Player& player = players_.at(command->player - 1);
      command::Command queued = command->command;
      // The Seen time keeps its delay: its tick moves onto the Replay's as the
      // Command's does. One before the World's first tick names no State.
      const std::int64_t seen = static_cast<std::int64_t>(FirstTick()) + command->seen_offset;
      queued.seen_tick = seen > 0 ? static_cast<tick::Tick>(seen) : 0;
      // A capture holds what passed the live queue, so only a doctored one is turned away here.
      (void)player.commands.TryEnqueue(SequencedCommand{.sequence = ++player.sequence, .command = queued});
    } else if (const auto* leave = std::get_if<capture::Leave>(&event.event)) {
      if (!LeavesAfterItsTick(events, next_event_, tick_run)) {
        players_.at(leave->player - 1).left = true;
        world_.RemovePlayer(BodyOf(leave->player));
      }
    } else {
      // A Death or the Match end: what the tick resolved, for the check alone.
      tick_run = true;
    }
  }
}

std::vector<simulation::PlayerCommand> Replay::TakeCommands(std::vector<PlayerView>& views) {
  std::vector<simulation::PlayerCommand> commands;
  for (Player& player : players_) {
    if (player.left) {
      continue;
    }
    const TickCommand next = player.commands.Next();
    commands.push_back(simulation::PlayerCommand{.entity = BodyOf(player.number), .command = next.command});
    views.push_back(PlayerView{
        .entity = static_cast<EntityId>(player.number), .pitch = next.command.pitch, .ads = next.command.ads});
  }
  return commands;
}

std::optional<ReplayEnd> Replay::EndOf(std::uint32_t offset, const simulation::TickResult& result) const {
  for (const policy::PolicyAction& action : result.actions) {
    if (const auto* end = std::get_if<policy::MatchEnd>(&action)) {
      return ReplayEnd{.winner = end->winner};
    }
  }
  if (offset < last_offset_) {
    return std::nullopt;
  }
  // The capture's Match end, the World not having ended it: as the capture says.
  ReplayEnd end;
  const auto* captured =
      capture_.events.empty() ? nullptr : std::get_if<capture::MatchEnd>(&capture_.events.back().event);
  if (captured != nullptr && captured->winner.has_value()) {
    end.winner = start_.players.at(*captured->winner - 1).session;
  }
  return end;
}

ReplayTick Replay::Step() {
  const std::uint32_t offset = offset_;
  HandBefore(offset);
  ReplayTick tick;
  const std::vector<simulation::PlayerCommand> commands = TakeCommands(tick.views);
  const float delta_time = 1.0F / static_cast<float>(capture_.header.tick_rate_hz);
  tick.result = world_.Tick(commands, delta_time);

  std::vector<capture::Death> deaths;
  for (const simulation::Death& death : tick.result.state.deaths) {
    deaths.push_back(capture::Death{.victim = static_cast<capture::Player>(death.victim),
                                    .killer = static_cast<capture::Player>(death.killer)});
  }
  std::optional<capture::MatchEnd> resolved_end;
  for (const policy::PolicyAction& action : tick.result.actions) {
    if (const auto* end = std::get_if<policy::MatchEnd>(&action)) {
      capture::MatchEnd captured;
      if (end->winner.has_value()) {
        const auto found = std::ranges::find(start_.players, *end->winner, &MatchPlayer::session);
        if (found != start_.players.end()) {
          captured.winner = static_cast<capture::Player>(found->entity);
        }
      }
      resolved_end = captured;
    }
  }
  tick.divergence = check_.Check(offset, std::move(deaths), resolved_end);
  tick.end = EndOf(offset, tick.result);
  ended_ = tick.end.has_value();
  ++offset_;
  return tick;
}

}  // namespace augusta::host
