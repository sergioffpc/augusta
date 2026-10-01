#include "host.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "command_queue.h"
#include "match.h"
#include "wire.h"

namespace augusta::server {

namespace {

// The authoritative world with the map's collision already in it. Built
// before the socket exists, so a map that is rejected never leaves a bound
// port behind.
simulation::World BuildSimulation(const HostConfig& config, const Map& map, scripting::Engine policy) {
  simulation::World simulation(config.parameters, config.tick_rate_hz, std::move(policy));
  for (const physics::CollisionMesh& mesh : map.collision) {
    if (const auto added = simulation.AddCollisionMesh(mesh); !added) {
      throw std::runtime_error(
          std::format("server::Host: map collision rejected: {}", physics::DescribeCollisionMeshError(added.error())));
    }
  }
  return simulation;
}

// The ticks of kMatchPause at tick_rate_hz, rounded up so the pause is never shorter.
std::uint32_t PauseTicks(std::uint8_t tick_rate_hz) {
  return static_cast<std::uint32_t>(std::ceil(std::chrono::duration<float>(kMatchPause).count() * tick_rate_hz));
}

// Every queue length fits the byte an Authoritative State update tells it in.
static_assert(kMaxQueuedCommands <= std::numeric_limits<std::uint8_t>::max());

// The transport's handle as a number, for log lines.
std::uint32_t PeerNumber(networking::PeerId peer) { return static_cast<std::uint32_t>(peer); }

std::uint32_t SessionNumber(SessionId session) { return static_cast<std::uint32_t>(session); }

ballistics::BodyPart ToBallistics(assets::BodyPart part) {
  switch (part) {
    case assets::BodyPart::kHead:
      return ballistics::BodyPart::kHead;
    case assets::BodyPart::kTorso:
      return ballistics::BodyPart::kTorso;
    case assets::BodyPart::kLimb:
      return ballistics::BodyPart::kLimb;
  }
  std::unreachable();
}

// hitbox, of character path, as the triangles a bullet is tested against.
// Throws std::runtime_error if its mesh is not a whole, in-range triangle list:
// a pack's mesh blob is not checked for that when it is decoded.
simulation::CharacterHitbox ToSimulation(const assets::HitboxData& hitbox, const std::string& path) {
  const assets::MeshData& mesh = hitbox.mesh;
  if (const auto valid = physics::ValidateCollisionMesh({.points = mesh.points, .indices = mesh.indices}); !valid) {
    throw std::runtime_error(std::format("server::Host: hitbox of character {} rejected: {}", path,
                                         physics::DescribeCollisionMeshError(valid.error())));
  }
  simulation::CharacterHitbox result{.part = ToBallistics(hitbox.part), .triangles = {}};
  result.triangles.reserve(mesh.indices.size() / 3);
  for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
    result.triangles.push_back(ballistics::Triangle{.a = mesh.points[mesh.indices[i]],
                                                    .b = mesh.points[mesh.indices[i + 1]],
                                                    .c = mesh.points[mesh.indices[i + 2]]});
  }
  return result;
}

// Each of characters as SimulationWorld takes it, in the same order. Built
// with the simulation, before the socket exists, for the same reason.
std::vector<simulation::Character> ToSimulation(const std::vector<Character>& characters) {
  std::vector<simulation::Character> result;
  result.reserve(characters.size());
  for (const Character& character : characters) {
    simulation::Character converted{.eye = character.eye, .hitboxes = {}};
    converted.hitboxes.reserve(character.hitboxes.size());
    for (const assets::HitboxData& hitbox : character.hitboxes) {
      converted.hitboxes.push_back(ToSimulation(hitbox, character.path));
    }
    result.push_back(std::move(converted));
  }
  return result;
}

std::string_view BodyPartName(ballistics::BodyPart part) {
  switch (part) {
    case ballistics::BodyPart::kHead:
      return "head";
    case ballistics::BodyPart::kTorso:
      return "torso";
    case ballistics::BodyPart::kLimb:
      return "limb";
  }
  std::unreachable();
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

simulation::EntityId ToSimulation(EntityId entity) {
  return static_cast<simulation::EntityId>(static_cast<std::uint32_t>(entity));
}

EntityId FromSimulation(simulation::EntityId entity) {
  return static_cast<EntityId>(static_cast<std::uint32_t>(entity));
}

struct Host::Impl {
  // What the server keeps per joined client.
  struct Player {
    networking::PeerId peer;
    CommandQueue commands;
  };

  // Declared before the socket so it is constructed first; see BuildSimulation.
  // Simulation thread only, with the body each player in it controls.
  simulation::World simulation;
  std::unordered_map<SessionId, EntityId> bodies;
  // The last tick SimulationWorld ran, as it numbers them: what its State was
  // sent under, and what a client names the view of its Commands by.
  std::uint64_t tick = 0;
  // What every client is told when it joins, with the tick rate; neither ever
  // changes, so neither needs the lock.
  const std::uint8_t tick_rate_hz;
  const parameters::Parameters parameters;
  // The scenario's characters as SimulationWorld takes them, by index - 1
  // (ADR-0042); never changes either.
  const std::vector<simulation::Character> characters;

  // Thread-safe by the transport's contract, used from both threads.
  networking::Server network;

  // Guards everything below: written by the Network I/O thread as clients
  // join, leave and send commands, and by the Simulation thread as matches
  // start and end.
  std::mutex mutex;
  Match match;
  std::unordered_map<SessionId, Player> players;

  // What Network I/O and the ticks did since the last heartbeat. Guarded by mutex.
  struct Activity {
    std::uint32_t ticks = 0;
    // Ticks that started after their deadline, and ticks whose work took
    // longer than a tick (NFR-01).
    std::uint32_t late = 0;
    std::uint32_t overrun = 0;
    std::uint32_t messages = 0;
    // Commands turned away as already handled, or as sent outside a match:
    // routine, since commands repeat and some are in flight when a match ends.
    std::uint32_t stale = 0;
    // Messages and commands refused for being malformed, or sent out of turn.
    std::uint32_t dropped = 0;
    // Queued commands dropped because a client ran further ahead than
    // kMaxQueuedCommands: its pacing is not keeping up.
    std::uint32_t overflow = 0;
  };
  static constexpr std::chrono::seconds kHeartbeatInterval{1};
  Activity activity;
  std::chrono::steady_clock::time_point activity_since = std::chrono::steady_clock::now();
  // A peer can send malformed messages as fast as it likes, so their warnings
  // are limited; the heartbeat still counts every one. Guarded by mutex.
  logging::Throttle drop_warnings{std::chrono::seconds{1}};

  Impl(const HostConfig& config, Map map, scripting::Engine policy)
      : simulation(BuildSimulation(config, map, std::move(policy))),
        tick_rate_hz(config.tick_rate_hz),
        parameters(config.parameters),
        characters(ToSimulation(map.characters)),
        network(config.listen),
        match(
            MatchConfig{
                .engine_version = std::string(EngineVersion()),
                .client_pack = map.client_pack,
                .characters = CharacterPaths(map.characters),
                .player_count = config.parameters.player_count,
                .pause_ticks = PauseTicks(config.tick_rate_hz),
            },
            std::move(map.spawn_points)) {}

  void Reply(networking::PeerId peer, const protocol::MessageWire& message) {
    network.Send(peer, protocol::Encode(message), networking::Reliability::kReliable);
  }

  // Sends message reliably to the player of each of sessions.
  void SendTo(const std::vector<SessionId>& sessions, const protocol::MessageWire& message) {
    const protocol::BytesWire payload = protocol::Encode(message);
    for (const SessionId session : sessions) {
      network.Send(players.at(session).peer, payload, networking::Reliability::kReliable);
    }
  }

  // Tells everyone in the Lobby who is in it, after it changed.
  void SendRoster() {
    const Roster roster = match.GetRoster();
    std::vector<SessionId> sessions;
    sessions.reserve(roster.players.size());
    for (const RosterEntry& entry : roster.players) {
      sessions.push_back(entry.session);
    }
    SendTo(sessions, ToWire(roster));
  }

  void HandleJoinRequest(networking::PeerId peer, const JoinRequest& request) {
    const auto admission = match.Join(peer, request);
    if (!admission.has_value()) {
      LI("subsystem=serverruntime event=join_refused peer={} reason=\"{}\"", PeerNumber(peer),
         DescribeJoinRefusal(admission.error()));
      Reply(peer, protocol::JoinRefusedWire{.reason = ToWire(admission.error())});
      return;
    }
    Reply(peer, ToWire(*admission, tick_rate_hz, parameters));
    if (players.try_emplace(admission->session, Player{.peer = peer, .commands = CommandQueue{tick_rate_hz}}).second) {
      LI("subsystem=serverruntime event=lobby_joined peer={} session={} character={} players={} version={}",
         PeerNumber(peer), SessionNumber(admission->session), admission->character, match.PlayerCount(),
         match.GetRoster().version);
      SendRoster();
    }
  }

  void HandleReady(networking::PeerId peer, std::uint32_t version) {
    if (match.Ready(peer, version)) {
      LI("subsystem=serverruntime event=ready peer={} version={}", PeerNumber(peer), version);
    } else {
      // The Roster can change while a Ready is in flight; the client sends another for the new one.
      LD("subsystem=serverruntime event=ready_ignored peer={} version={} current={}", PeerNumber(peer), version,
         match.GetRoster().version);
    }
  }

  void HandleCommands(networking::PeerId peer, const std::vector<SequencedCommand>& commands) {
    const std::optional<SessionId> session = match.SessionOf(peer);
    if (!session.has_value()) {
      ++activity.dropped;
      LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped peer={} reason=\"commands before joining\"",
                 PeerNumber(peer));
      return;
    }
    if (!match.IsPlaying(*session)) {
      ++activity.stale;
      LT("subsystem=serverruntime event=dropped peer={} reason=\"commands outside a match\"", PeerNumber(peer));
      return;
    }
    CommandQueue& queue = players.at(*session).commands;
    for (const SequencedCommand& command : commands) {
      const auto enqueued = queue.TryEnqueue(command);
      if (!enqueued.has_value()) {
        RecordRejection(peer, command, enqueued.error());
      } else if (*enqueued == Enqueued::kDroppedOldest) {
        ++activity.overflow;
      }
    }
  }

  // Counts and logs a command the queue turned away.
  void RecordRejection(networking::PeerId peer, const SequencedCommand& command, Rejection rejection) {
    // Commands are repeated until acknowledged, so a stale one is routine.
    if (rejection == Rejection::kStale) {
      ++activity.stale;
      LT("subsystem=serverruntime event=dropped peer={} sequence={} reason=\"{}\"", PeerNumber(peer), command.sequence,
         DescribeRejection(rejection));
    } else {
      ++activity.dropped;
      LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped_malformed peer={} sequence={} reason=\"{}\"",
                 PeerNumber(peer), command.sequence, DescribeRejection(rejection));
    }
  }

  void HandleMessage(const networking::PeerMessage& message) {
    const std::expected<protocol::MessageWire, protocol::DecodeError> decoded = protocol::Decode(message.payload);
    if (!decoded.has_value()) {
      ++activity.dropped;
      LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped_malformed peer={} bytes={} reason=\"{}\"",
                 PeerNumber(message.from), message.payload.size(), protocol::DescribeDecodeError(decoded.error()));
      return;
    }
    if (const auto* request = std::get_if<protocol::JoinRequestWire>(&*decoded)) {
      HandleJoinRequest(message.from, FromWire(*request));
    } else if (const auto* commands = std::get_if<protocol::CommandsWire>(&*decoded)) {
      HandleCommands(message.from, FromWire(*commands));
    } else if (const auto* ready = std::get_if<protocol::ReadyWire>(&*decoded)) {
      HandleReady(message.from, ready->version);
    } else {
      ++activity.dropped;
      LW_LIMITED(drop_warnings,
                 "subsystem=serverruntime event=dropped_malformed peer={} bytes={} reason=\"not a client message\"",
                 PeerNumber(message.from), message.payload.size());
    }
  }

  // A player whose connection ended leaves at once; a body it had leaves the
  // simulation at the start of the next tick. reason only decides which event is logged.
  void HandleDisconnect(networking::PeerId peer, networking::DisconnectReason reason) {
    const std::optional<SessionId> session = match.SessionOf(peer);
    if (!session.has_value()) {
      return;
    }
    const Departure departure = match.Leave(peer);
    players.erase(*session);
    const bool lost = reason == networking::DisconnectReason::kConnectionLost;
    switch (departure) {
      case Departure::kNone:
        break;
      case Departure::kFromLobby:
        LI("subsystem=serverruntime event=lobby_left peer={} session={} lost={} players={} version={}",
           PeerNumber(peer), SessionNumber(*session), lost, match.PlayerCount(), match.GetRoster().version);
        SendRoster();
        break;
      case Departure::kFromMatch:
        LI("subsystem=serverruntime event=match_left peer={} session={} lost={} playing={}", PeerNumber(peer),
           SessionNumber(*session), lost, match.Playing().size());
        break;
      case Departure::kEndedMatch:
        LI("subsystem=serverruntime event=match_left peer={} session={} lost={} playing=0", PeerNumber(peer),
           SessionNumber(*session), lost);
        LI("subsystem=serverruntime event=match_ended reason=\"no players left\"");
        break;
    }
  }

  // Ends the match in progress, if any: its players are told, and are back in
  // the Lobby; their bodies leave the simulation at the start of the next tick.
  void EndMatch() {
    const std::vector<SessionId> ended = match.End();
    if (ended.empty()) {
      return;
    }
    SendTo(ended, protocol::MatchEndWire{});
    LI("subsystem=serverruntime event=match_ended players={} version={}", ended.size(), match.GetRoster().version);
    SendRoster();
  }

  // Starts a match if one can start: its players' bodies enter the simulation
  // at their spawn points, their commands start afresh, and they are told.
  void StartMatchIfReady() {
    const std::optional<MatchStart> start = match.TryStart();
    if (!start.has_value()) {
      return;
    }
    std::vector<SessionId> sessions;
    for (const MatchPlayer& player : start->players) {
      simulation.AddPlayer(ToSimulation(player.entity), player.spawn, characters.at(player.character - 1));
      bodies.emplace(player.session, player.entity);
      players.at(player.session).commands = CommandQueue{tick_rate_hz};
      sessions.push_back(player.session);
    }
    SendTo(sessions, ToWire(*start));
    LI("subsystem=serverruntime event=match_started tick={} players={}", tick, sessions.size());
  }

  // What a tick runs on: one command per player in the match, and who to send
  // the tick's state to.
  struct TickInput {
    std::vector<simulation::PlayerCommand> commands;
    std::vector<replication::Recipient> recipients;
    // The connection of each recipient, by the entity its player controls.
    std::unordered_map<EntityId, networking::PeerId> peers;
  };

  // Removes the bodies of players no longer in a match, starts a match if one
  // can start, then takes one command per player in it for this tick.
  TickInput PrepareTick() {
    const std::lock_guard<std::mutex> lock(mutex);
    std::erase_if(bodies, [&](const auto& body) {
      const auto& [session, entity] = body;
      if (match.IsPlaying(session)) {
        return false;
      }
      simulation.RemovePlayer(ToSimulation(entity));
      return true;
    });
    match.Tick();
    StartMatchIfReady();

    TickInput input;
    for (const SessionId session : match.Playing()) {
      Player& player = players.at(session);
      const EntityId entity = bodies.at(session);
      const TickCommand next = player.commands.Next();
      input.commands.push_back(simulation::PlayerCommand{.entity = ToSimulation(entity), .command = next.command});
      input.recipients.push_back(replication::Recipient{
          .entity = ToSimulation(entity),
          .acknowledged_sequence = next.acknowledged_sequence,
          .queued_commands = static_cast<std::uint8_t>(player.commands.Queued()),
      });
      input.peers.emplace(entity, player.peer);
    }
    return input;
  }

  // Once a second, one line of what the last second held: a line per tick or
  // per packet would bury the one that matters. Simulation thread only.
  void Heartbeat(const tick::Timing& timing) {
    const auto now = std::chrono::steady_clock::now();
    const std::lock_guard<std::mutex> lock(mutex);
    ++activity.ticks;
    activity.late += timing.late ? 1U : 0U;
    activity.overrun += timing.overrun ? 1U : 0U;
    if (now - activity_since < kHeartbeatInterval) {
      return;
    }
    LD("subsystem=serverruntime event=heartbeat tick={} players={} in_match={} ticks={} late={} overrun={} "
       "messages={} stale={} dropped={} overflow={}",
       tick, players.size(), match.InMatch(), activity.ticks, activity.late, activity.overrun, activity.messages,
       activity.stale, activity.dropped, activity.overflow);
    activity = Activity{};
    activity_since = now;
  }

  // One line per hit of the tick, then one per death. At INFO, though players
  // provoke them: a Match's hits and deaths are what its operator reads the log
  // for (US-12, US-13), in a Release build too.
  void LogCombat(const simulation::State& state) const {
    for (const simulation::Hit& hit : state.hits) {
      LI("subsystem=serverruntime event=hit tick={} shooter={} target={} part={} damage={} health={}", tick,
         std::to_underlying(hit.shooter), std::to_underlying(hit.target), BodyPartName(hit.part), hit.damage,
         hit.health);
    }
    for (const simulation::Death& death : state.deaths) {
      LI("subsystem=serverruntime event=death tick={} victim={} killer={} part={}", tick,
         std::to_underlying(death.victim), std::to_underlying(death.killer), BodyPartName(death.part));
    }
  }

  // Each recipient's update, which a newer one supersedes; then what must
  // arrive (ADR-0044): every Shot and every Death of the tick to all of them,
  // and each Hit confirmation to its shooter alone, if it is still in the match.
  void Send(const simulation::State& state, const TickInput& input) {
    for (const replication::Update& update : replication::PlanUpdates(state, tick, input.recipients)) {
      network.Send(input.peers.at(FromSimulation(update.recipient)), protocol::Encode(ToWire(update)),
                   networking::Reliability::kUnreliable);
    }
    for (const replication::Shot& shot : replication::PlanShots(state, tick)) {
      const protocol::BytesWire payload = protocol::Encode(ToWire(shot));
      for (const auto& [entity, peer] : input.peers) {
        network.Send(peer, payload, networking::Reliability::kReliable);
      }
    }
    for (const replication::HitConfirmation& hit : replication::PlanHitConfirmations(state)) {
      if (const auto shooter = input.peers.find(FromSimulation(hit.recipient)); shooter != input.peers.end()) {
        network.Send(shooter->second, protocol::Encode(ToWire(hit)), networking::Reliability::kReliable);
      }
    }
    for (const replication::Death& death : replication::PlanDeaths(state)) {
      const protocol::BytesWire payload = protocol::Encode(ToWire(death));
      for (const auto& [entity, peer] : input.peers) {
        network.Send(peer, payload, networking::Reliability::kReliable);
      }
    }
  }
};

Host::Host(const HostConfig& config, Map map, scripting::Engine policy)
    : impl_(std::make_unique<Impl>(config, std::move(map), std::move(policy))) {}

Host::~Host() = default;

void Host::PumpNetwork() {
  Impl& impl = *impl_;
  for (const networking::PeerEvent& event : impl.network.PumpEvents()) {
    switch (event.type) {
      case networking::PeerEventType::kConnectRequested:
        // Every connection is accepted, since a refusal is a message and needs
        // the connection to travel on; whether the peer joins the Lobby is
        // decided by its JoinRequestWire.
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
  const Impl::TickInput input = impl.PrepareTick();
  simulation::State state = impl.simulation.Tick(input.commands, delta_time);
  impl.tick = state.tick;
  impl.Send(state, input);
  impl.LogCombat(state);
  return state;
}

void Host::RecordTiming(const tick::Timing& timing) { impl_->Heartbeat(timing); }

void Host::EndMatch() {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->EndMatch();
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
