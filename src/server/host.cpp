#include "host.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "augusta/logging.h"
#include "augusta/map.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "content.h"
#include "heartbeat.h"
#include "host_impl.h"
#include "match.h"
#include "simulation_mapping.h"
#include "wire.h"

namespace augusta::server {

namespace {

// The authoritative world with the map's collision already in it. Built
// before the socket exists, so a map that is rejected never leaves a bound
// port behind.
simulation::World BuildSimulation(const HostConfig& config, const Scenario& scenario, scripting::Engine policy) {
  simulation::World simulation(config.parameters, config.tick_rate_hz, std::move(policy));
  if (const auto added = map::AddCollision(simulation, scenario.collision); !added) {
    throw std::runtime_error(
        std::format("server::Host: map collision rejected: {}", physics::DescribeCollisionMeshError(added.error())));
  }
  return simulation;
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
    : simulation(BuildSimulation(config, scenario, std::move(policy))),
      tick_rate_hz(config.tick_rate_hz),
      parameters(config.parameters),
      characters(ToSimulation(scenario.characters)),
      spawn_points(std::move(scenario.spawn_points)),
      network(config.listen),
      match(MatchConfig{
          .engine_version = std::string(EngineVersion()),
          .client_pack = scenario.client_pack,
          .characters = CharacterPaths(scenario.characters),
          .player_count = config.parameters.player_count,
          .pause_ticks = PauseTicks(config.tick_rate_hz),
      }) {}

void Host::Impl::Reply(networking::PeerId peer, const protocol::MessageWire& message) {
  network.Send(peer, protocol::Encode(message), networking::Reliability::kReliable);
}

void Host::Impl::SendTo(const std::vector<SessionId>& sessions, const protocol::MessageWire& message) {
  const protocol::BytesWire payload = protocol::Encode(message);
  for (const SessionId session : sessions) {
    network.Send(players.at(session).peer, payload, networking::Reliability::kReliable);
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

Host::Host(const HostConfig& config, Scenario scenario, scripting::Engine policy)
    : impl_(std::make_unique<Impl>(config, std::move(scenario), std::move(policy))) {}

Host::~Host() = default;

networking::Endpoint Host::ListenEndpoint() const { return impl_->network.LocalEndpoint(); }

void Host::RecordTiming(const tick::Timing& timing) {
  const auto now = std::chrono::steady_clock::now();
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  const std::optional<Activity> second = impl_->heartbeat.RecordTick(timing, now);
  if (!second.has_value()) {
    return;
  }
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

}  // namespace augusta::server
