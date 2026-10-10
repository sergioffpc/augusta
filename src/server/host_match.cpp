#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/failure.h"
#include "augusta/first_failure.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/policy_actions.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "capture.h"
#include "command_queue.h"
#include "host.h"
#include "host_impl.h"
#include "host_log.h"
#include "host_metrics.h"
#include "match.h"
#include "simulation_mapping.h"
#include "tick_messages.h"
#include "wire.h"

namespace augusta::server {

// Every queue length fits the byte an Authoritative State update tells it in.
static_assert(kMaxQueuedCommands <= std::numeric_limits<std::uint8_t>::max());

namespace {

// What the tick of result fired and hit, at what Shooter's delay, and how many
// bullets it left in flight.
void CountCombat(HostMetrics& metrics, const simulation::TickResult& result) {
  metrics.shots.Increment(result.state.shots.size());
  metrics.bullets_in_flight.Set(static_cast<double>(result.state.bullets_in_flight));
  for (const simulation::Hit& hit : result.state.hits) {
    metrics.hit_confirmations[hit.part].Increment();
  }
  const float cap = std::chrono::duration<float>(simulation::kMaxShootersDelay).count();
  for (const float delay : result.shooters_delays) {
    metrics.shooters_delay.Observe(delay);
    metrics.shooters_delay_capped.Increment(delay >= cap ? 1 : 0);
  }
}

}  // namespace

// One line for every match that ends (ADR-0029): why, who won, how many
// ticks it lasted, counting the one it ended on, and how many were in it.
void Host::Impl::LogMatchEnded(EndReason reason, const std::optional<SessionId>& winner, std::size_t playing) const {
  server::LogMatchEnded(reason, winner, MatchTicks(), playing, match.GetRoster().version);
}

// Counts a match that ended for reason, with winner or as a draw: how, and how
// long it lasted, counting the tick it ended on.
void Host::Impl::CountMatchEnded(EndReason reason, const std::optional<SessionId>& winner) {
  if (reason == EndReason::kNoPlayersLeft) {
    metrics.matches_ended_abandoned.Increment();
  } else if (winner.has_value()) {
    metrics.matches_ended_with_winner.Increment();
  } else {
    metrics.matches_ended_drawn.Increment();
  }
  metrics.match_duration.Observe(static_cast<double>(MatchTicks()) / tick_rate_hz);
}

// Ends the match in progress, if any, with winner or as a draw, for reason:
// its players are told, and are back in the Lobby; their bodies and the
// bullets in flight leave the simulation at the start of the next tick.
void Host::Impl::EndMatch(const std::optional<SessionId>& winner, EndReason reason) {
  const std::optional<MatchEnd> ended = match.End(winner);
  if (!ended.has_value()) {
    return;
  }
  SendTo(ended->players, EncodeToSend(ToWire(*ended)));
  match_winner = ended->winner;
  CountMatchEnded(reason, ended->winner);
  LogMatchEnded(reason, ended->winner, ended->players.size());
  SetLobbyGauges();
  SendRoster();
}

// Acts on Game policy's Match end, after the tick it was decided on.
void Host::Impl::Act(const simulation::MatchEnd& end) {
  const std::lock_guard<std::mutex> lock(mutex);
  EndMatch(end.winner.transform([](simulation::SessionId winner) { return FromSimulation(winner); }),
           EndReason::kWinCondition);
}

// Starts a match if one can start: its players' bodies enter the simulation
// at the Spawn points Game policy gives them, or a Captured player's at the
// spawn its Reenact request named, their commands start afresh, and they are
// told where each spawned.
void Host::Impl::StartMatchIfReady() {
  const std::optional<MatchStart> start = match.TryStart();
  if (!start.has_value()) {
    return;
  }
  std::vector<simulation::MatchPlayer> entrants;
  entrants.reserve(start->players.size());
  std::vector<SessionId> sessions;
  for (const MatchPlayer& player : start->players) {
    entrants.push_back(
        simulation::MatchPlayer{.entity = ToSimulation(player.entity),
                                .identity = {.session = ToSimulation(player.session), .character = player.character},
                                .character = characters.at(player.character),
                                .spawn = player.spawn});
    bodies.emplace(player.session, player.entity);
    players.at(player.session).commands = CommandQueue{tick_rate_hz};
    sessions.push_back(player.session);
  }
  const std::vector<math::Vec3> spawns = simulation.StartMatch(entrants, spawn_points);
  simulating_match = true;
  // Its first tick is the one about to run.
  match_start_tick = tick + 1;
  if (capturer) {
    std::vector<CaptureEntrant> captured;
    captured.reserve(start->players.size());
    for (std::size_t i = 0; i < start->players.size(); ++i) {
      const MatchPlayer& player = start->players[i];
      captured.push_back(CaptureEntrant{
          .session = player.session, .entity = player.entity, .character = player.character, .spawn = spawns[i]});
    }
    capturer->StartMatch(captured, match_start_tick, std::chrono::system_clock::now());
  }
  metrics.matches_started.Increment();
  SetLobbyGauges();
  SendTo(sessions, EncodeToSend(ToWire(*start, spawns, match_start_tick)));
  LI("subsystem=serverruntime event=match_started tick={} players={}", match_start_tick, sessions.size());
}

// Takes a match that has ended out of the simulation, its capture ending on
// the last tick it ran: those of its players who had left by then left on it.
// With mutex held.
void Host::Impl::TakeOutEndedMatch() {
  simulation.EndMatch();
  if (capturer) {
    for (const auto& [session, entity] : bodies) {
      if (!players.contains(session)) {
        capturer->Leave(tick, entity);
      }
    }
    capturer->EndMatch(tick, match_winner);
  }
  bodies.clear();
  simulating_match = false;
}

// Takes a match that has ended out of the simulation, or the bodies of
// players who left the one in progress, starts a match if one can start,
// then takes one command per player in it for this tick.
Host::Impl::TickInput Host::Impl::PrepareTick() {
  const std::lock_guard<std::mutex> lock(mutex);
  if (simulating_match && !match.InMatch()) {
    TakeOutEndedMatch();
  }
  // The tick about to run, which each of its events is captured at.
  const tick::Tick next_tick = tick + 1;
  std::erase_if(bodies, [&](const auto& body) {
    const auto& [session, entity] = body;
    if (match.IsPlaying(session)) {
      return false;
    }
    simulation.RemovePlayer(ToSimulation(entity));
    if (capturer) {
      capturer->Leave(next_tick, entity);
    }
    return true;
  });
  match.Tick();
  StartMatchIfReady();

  TickInput input;
  for (const SessionId session : match.Playing()) {
    Player& player = players.at(session);
    const EntityId entity = bodies.at(session);
    const TickCommand next = player.commands.Next();
    if (capturer && next.sent) {
      capturer->Command(next_tick, entity, next.command);
    }
    input.commands.push_back(simulation::PlayerCommand{.entity = ToSimulation(entity), .command = next.command});
    input.to.recipients.push_back(replication::Recipient{
        .entity = ToSimulation(entity),
        .acknowledged_sequence = next.acknowledged_sequence,
        .queued_commands = static_cast<std::uint8_t>(player.commands.Queued()),
    });
    input.to.peers.emplace(entity, player.peer);
  }
  return input;
}

// Captures each Death of the tick of result, if Matches are captured.
void Host::Impl::CaptureDeaths(const simulation::TickResult& result) const {
  if (!capturer) {
    return;
  }
  for (const simulation::Death& death : result.state.deaths) {
    capturer->Death(result.state.tick, FromSimulation(death.victim), FromSimulation(death.killer));
  }
}

void Host::Impl::SendTick(const simulation::State& state, const TickRecipients& to) {
  // Asked before each message: a failure the Network I/O thread records
  // mid-tick stops the rest of the tick's messages too.
  if (auto sent = SendTickMessages(network, metrics, state, tick, to, [this] { return Failed(); }); !sent.has_value()) {
    // Either the tick's messages could not be encoded, and none was sent, or
    // the local transport failed sending them: each kept where a worker takes it.
    failure::FirstFailure& kept =
        sent.error().code == failure::Code::kInvariantViolated ? invariant_failure : transport_failure;
    kept.Record(std::move(sent.error()));
  }
}

simulation::TickResult Host::Tick(float delta_time) {
  Impl& impl = *impl_;
  const Impl::TickInput input = impl.PrepareTick();
  const simulation::TickResult result = impl.simulation.Tick(input.commands, delta_time);
  impl.tick = result.state.tick;
  impl.CaptureDeaths(result);
  if (impl.capturer && impl.capturer->Failure().has_value()) {
    impl.invariant_failure.Record(*impl.capturer->Failure());
  }
  impl.SendTick(result.state, input.to);
  impl.metrics.match_players_alive.Set(static_cast<double>(result.state.alive.size()));
  CountCombat(impl.metrics, result);
  LogCombat(result.state);
  // After the tick's own messages, so a client hears the deaths that ended the
  // match before it hears that it has.
  for (const simulation::PolicyAction& action : result.actions) {
    std::visit([&impl](const auto& typed) { impl.Act(typed); }, action);
  }
  return result;
}

void Host::EndMatch() {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->EndMatch(std::nullopt, EndReason::kEndedByTheHost);
}

}  // namespace augusta::server
