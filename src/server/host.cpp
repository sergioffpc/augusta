#include "host.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/logging.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/version.h"
#include "command_queue.h"
#include "match.h"

namespace augusta::server {

namespace {

// The authoritative world with the map's collision already in it. Built
// before the socket exists, so a map that is rejected never leaves a bound
// port behind.
simulation::World BuildSimulation(const HostConfig& config) {
  simulation::World simulation(config.parameters.stamina, config.script_path);
  for (const physics::CollisionMesh& mesh : config.collision) {
    if (const auto added = simulation.AddCollisionMesh(mesh); !added) {
      throw std::runtime_error(
          std::format("server::Host: map collision rejected: {}", physics::DescribeCollisionMeshError(added.error())));
    }
  }
  return simulation;
}

// The transport's handle as a number, for log lines.
std::uint32_t PeerNumber(networking::PeerId peer) { return static_cast<std::uint32_t>(peer); }

}  // namespace

struct Host::Impl {
  // A player joining or leaving, for the Simulation thread to apply at the start of its next tick.
  struct Change {
    enum class Kind : std::uint8_t { kJoin, kLeave };
    Kind kind;
    protocol::SessionId session;
    // Where a joining player is put; unused for a leave.
    math::Vec3 spawn{};
  };

  // What the server keeps per joined client.
  struct Player {
    networking::PeerId peer;
    CommandQueue commands;
  };

  // Declared before the socket so it is constructed first; see BuildSimulation.
  // Simulation thread only.
  simulation::World simulation;
  std::uint32_t tick = 0;
  // What every client is told when it joins, with the tick rate; neither ever
  // changes, so neither needs the lock.
  const float tick_rate_hz;
  const parameters::Parameters parameters;

  // Thread-safe by the transport's contract, used from both threads.
  networking::Server network;

  // Guards everything below: written by the Network I/O thread as clients
  // join, leave and send commands, and read once per Simulation tick.
  std::mutex mutex;
  Match match;
  std::unordered_map<protocol::SessionId, Player> players;
  std::vector<Change> changes;

  // What Network I/O and the ticks did since the last heartbeat. Guarded by mutex.
  struct Activity {
    std::uint32_t ticks = 0;
    std::uint32_t messages = 0;
    // Commands the queue turned away as already handled; routine, since commands repeat.
    std::uint32_t stale = 0;
    // Messages and commands refused for being malformed, or sent out of turn.
    std::uint32_t dropped = 0;
  };
  static constexpr std::chrono::seconds kHeartbeatInterval{1};
  Activity activity;
  std::chrono::steady_clock::time_point activity_since = std::chrono::steady_clock::now();
  // A peer can send malformed messages as fast as it likes, so their warnings
  // are limited; the heartbeat still counts every one. Guarded by mutex.
  logging::Throttle drop_warnings{std::chrono::seconds{1}};

  explicit Impl(const HostConfig& config)
      : simulation(BuildSimulation(config)),
        tick_rate_hz(config.tick_rate_hz),
        parameters(config.parameters),
        network(config.listen),
        match(std::string(EngineVersion()), protocol::kMaxPlayers, config.spawn_points) {}

  void Reply(networking::PeerId peer, const protocol::Message& message) {
    network.Send(peer, protocol::Encode(message), networking::Reliability::kReliable);
  }

  void HandleJoinRequest(networking::PeerId peer, const protocol::JoinRequest& request) {
    const auto admission = match.Join(peer, request.engine_version);
    if (!admission.has_value()) {
      LI("subsystem=serverruntime event=join_refused peer={} reason=\"{}\"", PeerNumber(peer),
         protocol::DescribeJoinRefusal(admission.error()));
      Reply(peer, protocol::JoinRefused{.reason = admission.error()});
      return;
    }
    if (players.try_emplace(admission->session, Player{.peer = peer, .commands = {}}).second) {
      changes.push_back(Change{.kind = Change::Kind::kJoin, .session = admission->session, .spawn = admission->spawn});
      LI("subsystem=serverruntime event=joined peer={} players={}", PeerNumber(peer), match.PlayerCount());
    }
    protocol::JoinAccepted accepted;
    accepted.session = admission->session;
    accepted.spawn = admission->spawn;
    accepted.tick_rate_hz = tick_rate_hz;
    accepted.parameters = parameters;
    accepted.roster = admission->roster;
    Reply(peer, accepted);
  }

  void HandleCommands(networking::PeerId peer, const protocol::Commands& message) {
    const std::optional<protocol::SessionId> session = match.SessionOf(peer);
    if (!session.has_value()) {
      ++activity.dropped;
      LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped peer={} reason=\"commands before joining\"",
                 PeerNumber(peer));
      return;
    }
    CommandQueue& queue = players.at(*session).commands;
    for (const protocol::SequencedCommand& command : message.commands) {
      const auto offered = queue.Offer(command);
      if (offered.has_value()) {
        continue;
      }
      // Commands are repeated until acknowledged, so a stale one is routine.
      if (offered.error() == Rejection::kStale) {
        ++activity.stale;
        LT("subsystem=serverruntime event=dropped peer={} sequence={} reason=\"{}\"", PeerNumber(peer),
           command.sequence, DescribeRejection(offered.error()));
      } else {
        ++activity.dropped;
        LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped_malformed peer={} sequence={} reason=\"{}\"",
                   PeerNumber(peer), command.sequence, DescribeRejection(offered.error()));
      }
    }
  }

  void HandleMessage(const networking::PeerMessage& message) {
    const std::expected<protocol::Message, protocol::DecodeError> decoded = protocol::Decode(message.payload);
    if (!decoded.has_value()) {
      ++activity.dropped;
      LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped_malformed peer={} bytes={} reason=\"{}\"",
                 PeerNumber(message.from), message.payload.size(), protocol::DescribeDecodeError(decoded.error()));
      return;
    }
    if (const auto* request = std::get_if<protocol::JoinRequest>(&*decoded)) {
      HandleJoinRequest(message.from, *request);
    } else if (const auto* commands = std::get_if<protocol::Commands>(&*decoded)) {
      HandleCommands(message.from, *commands);
    } else {
      ++activity.dropped;
      LW_LIMITED(drop_warnings,
                 "subsystem=serverruntime event=dropped_malformed peer={} bytes={} reason=\"not a client message\"",
                 PeerNumber(message.from), message.payload.size());
    }
  }

  // A player whose connection ended leaves at the start of the next tick; the
  // slot is free at once. reason only decides which event is logged.
  void HandleDisconnect(networking::PeerId peer, networking::DisconnectReason reason) {
    const std::optional<protocol::SessionId> session = match.SessionOf(peer);
    if (!session.has_value()) {
      return;
    }
    match.Leave(peer);
    players.erase(*session);
    changes.push_back(Change{.kind = Change::Kind::kLeave, .session = *session});
    if (reason == networking::DisconnectReason::kConnectionLost) {
      LW("subsystem=serverruntime event=timeout peer={} players={}", PeerNumber(peer), match.PlayerCount());
    } else {
      LI("subsystem=serverruntime event=left peer={} players={}", PeerNumber(peer), match.PlayerCount());
    }
  }

  // Applies the joins and leaves since the last tick to the simulation, then
  // takes one command per player for this tick. Also returns who to send the
  // tick's state to.
  struct TickInput {
    std::vector<simulation::PlayerCommand> commands;
    std::vector<replication::Recipient> recipients;
    std::unordered_map<protocol::SessionId, networking::PeerId> peers;
  };

  TickInput BeginTick() {
    const std::lock_guard<std::mutex> lock(mutex);
    for (const Change& change : changes) {
      const simulation::PlayerId player = replication::PlayerOf(change.session);
      if (change.kind == Change::Kind::kJoin) {
        simulation.AddPlayer(player, change.spawn);
      } else {
        simulation.RemovePlayer(player);
      }
    }
    changes.clear();

    TickInput input;
    for (auto& [session, player] : players) {
      const TickCommand next = player.commands.Next();
      input.commands.push_back(
          simulation::PlayerCommand{.player = replication::PlayerOf(session), .command = next.command});
      input.recipients.push_back(
          replication::Recipient{.session = session, .acknowledged_sequence = next.acknowledged_sequence});
      input.peers.emplace(session, player.peer);
    }
    return input;
  }

  // Tells the match where everyone is, for the roster of whoever joins next.
  void RememberBodies(const simulation::State& state) {
    const std::lock_guard<std::mutex> lock(mutex);
    for (const simulation::PlayerState& player : state.players) {
      match.UpdateBody(replication::SessionOf(player.player), player.body);
    }
  }

  // Once a second, one line of what the last second held: a line per tick or
  // per packet would bury the one that matters. Simulation thread only.
  void Heartbeat() {
    const auto now = std::chrono::steady_clock::now();
    const std::lock_guard<std::mutex> lock(mutex);
    ++activity.ticks;
    if (now - activity_since < kHeartbeatInterval) {
      return;
    }
    LD("subsystem=serverruntime event=heartbeat tick={} players={} ticks={} messages={} stale={} dropped={}", tick,
       players.size(), activity.ticks, activity.messages, activity.stale, activity.dropped);
    activity = Activity{};
    activity_since = now;
  }

  void Send(const simulation::State& state, const TickInput& input) {
    for (const replication::Update& update : replication::PlanUpdates(state, tick, input.recipients)) {
      network.Send(input.peers.at(update.recipient), protocol::Encode(update.state),
                   networking::Reliability::kUnreliable);
    }
  }
};

Host::Host(const HostConfig& config) : impl_(std::make_unique<Impl>(config)) {}

Host::~Host() = default;

void Host::PumpNetwork() {
  Impl& impl = *impl_;
  for (const networking::PeerEvent& event : impl.network.PumpEvents()) {
    switch (event.type) {
      case networking::PeerEventType::kConnectRequested:
        // Every connection is accepted, since a refusal is a message and needs
        // the connection to travel on; whether the peer joins the match is
        // decided by its JoinRequest.
        impl.network.Accept(event.peer);
        break;
      case networking::PeerEventType::kConnected:
        break;
      case networking::PeerEventType::kDisconnected: {
        const std::lock_guard<std::mutex> lock(impl.mutex);
        impl.HandleDisconnect(event.peer, event.reason);
        break;
      }
    }
  }
  for (const networking::PeerMessage& message : impl.network.ReceiveMessages()) {
    LT("subsystem=serverruntime event=received peer={} bytes={}", PeerNumber(message.from), message.payload.size());
    const std::lock_guard<std::mutex> lock(impl.mutex);
    ++impl.activity.messages;
    impl.HandleMessage(message);
  }
}

simulation::State Host::Tick(float delta_time) {
  Impl& impl = *impl_;
  const Impl::TickInput input = impl.BeginTick();
  simulation::State state = impl.simulation.Tick(input.commands, delta_time);
  ++impl.tick;
  impl.RememberBodies(state);
  impl.Send(state, input);
  impl.Heartbeat();
  return state;
}

}  // namespace augusta::server
