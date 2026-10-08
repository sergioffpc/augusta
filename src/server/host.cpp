#include "host.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "augusta/failure.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/protocol.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "content.h"
#include "heartbeat.h"
#include "host_impl.h"
#include "host_metrics.h"
#include "match.h"
#include "recording.h"
#include "simulation_mapping.h"
#include "tick_messages.h"
#include "wire.h"

namespace augusta::server {

namespace {

// The authoritative world with the map's collision already in it, recording
// to file if config asks for a recording (ADR-0048). Built before the socket
// exists, so a map that is rejected never leaves a bound port behind.
RecordedSimulation BuildRecordedSimulation(const HostConfig& config, const Scenario& scenario, scripting::Engine policy,
                                           std::ofstream& file) {
  simulation::World world = BuildSimulation(config.parameters, config.tick_rate_hz, scenario, std::move(policy));
  if (config.recording.empty()) {
    return {std::move(world), std::nullopt};
  }
  file.open(config.recording, std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error(std::format("server::Host: cannot write a recording to {}", config.recording.string()));
  }
  return {std::move(world), Recorder(file, RecordingHeader{.engine_version = std::string(EngineVersion()),
                                                           .server_pack = config.server_pack,
                                                           .tick_rate_hz = config.tick_rate_hz})};
}

// The ticks of kMatchPause at tick_rate_hz, rounded up so the pause is never shorter.
std::uint32_t PauseTicks(std::uint8_t tick_rate_hz) {
  return static_cast<std::uint32_t>(std::ceil(std::chrono::duration<float>(kMatchPause).count() * tick_rate_hz));
}

// The path of each of characters, in the same order: all Match needs of them.
std::vector<std::string> CharacterPaths(const std::vector<Character>& characters) {
  std::vector<std::string> paths;
  paths.reserve(characters.size());
  for (const Character& character : characters) {
    paths.push_back(character.path);
  }
  return paths;
}

}  // namespace

Host::Impl::Impl(const HostConfig& config, Scenario scenario, scripting::Engine policy)
    : simulation(BuildRecordedSimulation(config, scenario, std::move(policy), recording_file)),
      tick_rate_hz(config.tick_rate_hz),
      parameters(config.parameters),
      characters(ToSimulation(scenario.characters)),
      spawn_points(std::move(scenario.spawn_points)),
      network(config.listen),
      metrics(config.tick_rate_hz),
      match(MatchConfig{
          .engine_version = std::string(EngineVersion()),
          .client_pack = scenario.client_pack,
          .characters = CharacterPaths(scenario.characters),
          .player_count = config.parameters.player_count,
          .pause_ticks = PauseTicks(config.tick_rate_hz),
      }) {}

void Host::Impl::Fail(failure::Failure broken) {
  const std::lock_guard<std::mutex> lock(failure_mutex);
  if (!failure.has_value()) {
    failure = std::move(broken);
  }
}

void Host::Impl::Reply(networking::PeerId peer, const protocol::MessageWire& message) {
  const auto payload = EncodeToSend(message);
  if (!payload.has_value()) {
    Fail(payload.error());
    return;
  }
  SendCounted(network, metrics, peer, *payload, networking::Reliability::kReliable);
}

void Host::Impl::SendTo(const std::vector<SessionId>& sessions, const protocol::MessageWire& message) {
  const auto payload = EncodeToSend(message);
  if (!payload.has_value()) {
    Fail(payload.error());
    return;
  }
  for (const SessionId session : sessions) {
    SendCounted(network, metrics, players.at(session).peer, *payload, networking::Reliability::kReliable);
  }
}

void Host::Impl::SendRoster() {
  const Roster roster = match.GetRoster();
  std::vector<SessionId> sessions;
  sessions.reserve(roster.players.size());
  for (const RosterEntry& entry : roster.players) {
    sessions.push_back(entry.session);
  }
  SendTo(sessions, ToWire(roster));
}

void Host::Impl::SetLobbyGauges() {
  metrics.sessions.Set(static_cast<double>(players.size()));
  metrics.lobby_players.Set(static_cast<double>(match.GetRoster().players.size()));
  metrics.match_in_progress.Set(match.InMatch() ? 1.0 : 0.0);
}

Host::Host(const HostConfig& config, Scenario scenario, scripting::Engine policy)
    : impl_(std::make_unique<Impl>(config, std::move(scenario), std::move(policy))) {}

Host::~Host() = default;

networking::Endpoint Host::ListenEndpoint() const { return impl_->network.LocalEndpoint(); }

std::optional<failure::Failure> Host::InvariantFailure() const {
  const std::lock_guard<std::mutex> lock(impl_->failure_mutex);
  return impl_->failure;
}

void Host::RecordTiming(const tick::Timing& timing) {
  HostMetrics& metrics = impl_->metrics;
  metrics.tick_duration.Observe(std::chrono::duration<double>(timing.duration).count());
  metrics.ticks.Increment();
  metrics.ticks_late.Increment(timing.late ? 1 : 0);
  metrics.tick_overruns.Increment(timing.overrun ? 1 : 0);
  metrics.tick_resyncs.Increment(timing.resynchronised ? 1 : 0);
  const std::optional<Activity> second = impl_->heartbeat.Record(Totals(metrics), std::chrono::steady_clock::now());
  if (!second.has_value()) {
    return;
  }
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  LD("subsystem=serverruntime event=heartbeat tick={} players={} in_match={} ticks={} late={} overrun={} "
     "messages={} stale={} dropped={} overflow={} misbehaving={}",
     impl_->tick.load(), impl_->players.size(), impl_->match.InMatch(), second->ticks, second->late, second->overrun,
     second->messages, second->stale, second->dropped, second->overflow, second->misbehaving);
}

std::size_t Host::QueuedCommands(SessionId session) const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto player = impl_->players.find(session);
  if (!impl_->match.IsPlaying(session) || player == impl_->players.end()) {
    return 0;
  }
  return player->second.commands.Queued();
}

const HostMetrics& Host::Metrics() const { return impl_->metrics; }

}  // namespace augusta::server
