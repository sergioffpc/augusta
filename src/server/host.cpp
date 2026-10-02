#include "host.h"

#include <atomic>
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
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/policy_actions.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "command_queue.h"
#include "match.h"
#include "misbehaviour.h"
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

// session as SimulationWorld's Game policy names the same player, and back.
simulation::SessionId ToSimulation(SessionId session) {
  return static_cast<simulation::SessionId>(SessionNumber(session));
}

SessionId FromSimulation(simulation::SessionId session) { return static_cast<SessionId>(std::to_underlying(session)); }

// What a Match end's log line names as its winner.
std::string WinnerName(const std::optional<SessionId>& winner) {
  return winner.has_value() ? std::to_string(SessionNumber(*winner)) : "draw";
}

// Why a match ended.
enum class EndReason : std::uint8_t {
  // Game policy decided it was over (US-14).
  kWinCondition,
  // Its last player left.
  kNoPlayersLeft,
  // Host::EndMatch was called.
  kEndedByTheHost,
};

// reason as a match end's log line says it (ADR-0029).
std::string_view EndReasonName(EndReason reason) {
  switch (reason) {
    case EndReason::kWinCondition:
      return "win condition";
    case EndReason::kNoPlayersLeft:
      return "no players left";
    case EndReason::kEndedByTheHost:
      return "ended by the host";
  }
  std::unreachable();
}

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

// How a player's connection ended.
enum class Leaving : std::uint8_t {
  // The peer closed it.
  kLeft,
  // The transport gave up on it.
  kTimedOut,
  // This side closed it, for misbehaving.
  kMisbehaving,
};

// how as a departure's log line says it (ADR-0038).
std::string_view LeavingName(Leaving how) {
  switch (how) {
    case Leaving::kLeft:
      return "left";
    case Leaving::kTimedOut:
      return "timeout";
    case Leaving::kMisbehaving:
      return "misbehaving";
  }
  std::unreachable();
}

// rejection, of one of a peer's commands, as the peer's misbehaviour is counted.
PeerRejection ToPeerRejection(Rejection rejection) {
  switch (rejection) {
    case Rejection::kStale:
      return PeerRejection::kStaleCommand;
    case Rejection::kNonFinite:
      return PeerRejection::kNonFiniteCommand;
    case Rejection::kOutOfRange:
      return PeerRejection::kOutOfRangeCommand;
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
  // Simulation thread only, with the body each player in it controls, and
  // whether the match those bodies are in is still in the simulation.
  simulation::World simulation;
  std::unordered_map<SessionId, EntityId> bodies;
  bool simulating_match = false;
  // The last tick SimulationWorld ran, as it numbers them: what its State was
  // sent under, and what a client names the view of its Commands by. Written by
  // the Simulation thread; read by the Network I/O thread too, to log how long
  // a match its last player left lasted.
  std::atomic<tick::Tick> tick = 0;
  // What every client is told when it joins, with the tick rate; neither ever
  // changes, so neither needs the lock.
  const std::uint8_t tick_rate_hz;
  const parameters::Parameters parameters;
  // The scenario's characters as SimulationWorld takes them, by index - 1
  // (ADR-0042), and the Map's Spawn points; neither changes either.
  const std::vector<simulation::Character> characters;
  const std::vector<math::Vec3> spawn_points;

  // Thread-safe by the transport's contract, used from both threads.
  networking::Server network;

  // Guards everything below: written by the Network I/O thread as clients
  // join, leave and send commands, and by the Simulation thread as matches
  // start and end.
  std::mutex mutex;
  Match match;
  std::unordered_map<SessionId, Player> players;
  // The tick the match in progress, or the last one, started on.
  tick::Tick match_start_tick = 0;

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
    // Peers disconnected for misbehaving.
    std::uint32_t misbehaving = 0;
  };
  static constexpr std::chrono::seconds kHeartbeatInterval{1};
  Activity activity;
  std::chrono::steady_clock::time_point activity_since = std::chrono::steady_clock::now();
  // A peer can send malformed messages as fast as it likes, so their warnings
  // are limited; the heartbeat still counts every one. Guarded by mutex.
  logging::Throttle drop_warnings{std::chrono::seconds{1}};
  // Each connected peer's misbehaviour, and the peers disconnected for it during
  // this PumpNetwork, whose messages still in its batch are ignored. Guarded by mutex.
  std::unordered_map<networking::PeerId, MisbehaviourTracker> misbehaviour;
  std::unordered_set<networking::PeerId> expelled;

  Impl(const HostConfig& config, Map map, scripting::Engine policy)
      : simulation(BuildSimulation(config, map, std::move(policy))),
        tick_rate_hz(config.tick_rate_hz),
        parameters(config.parameters),
        characters(ToSimulation(map.characters)),
        spawn_points(std::move(map.spawn_points)),
        network(config.listen),
        match(MatchConfig{
            .engine_version = std::string(EngineVersion()),
            .client_pack = map.client_pack,
            .characters = CharacterPaths(map.characters),
            .player_count = config.parameters.player_count,
            .pause_ticks = PauseTicks(config.tick_rate_hz),
        }) {}

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
      Judge(peer, PeerRejection::kJoinRefused);
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
      Judge(peer, PeerRejection::kStaleReady);
    }
  }

  void HandleCommands(networking::PeerId peer, const std::vector<SequencedCommand>& commands) {
    const std::optional<SessionId> session = match.SessionOf(peer);
    if (!session.has_value()) {
      ++activity.dropped;
      LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped peer={} reason=\"commands before joining\"",
                 PeerNumber(peer));
      Judge(peer, PeerRejection::kCommandsBeforeJoining);
      return;
    }
    if (!match.IsPlaying(*session)) {
      ++activity.stale;
      LT("subsystem=serverruntime event=dropped peer={} reason=\"commands outside a match\"", PeerNumber(peer));
      Judge(peer, PeerRejection::kCommandsOutsideMatch);
      return;
    }
    CommandQueue& queue = players.at(*session).commands;
    for (const SequencedCommand& command : commands) {
      const auto enqueued = queue.TryEnqueue(command);
      if (!enqueued.has_value()) {
        RecordRejection(peer, command, enqueued.error());
        // A disconnected player's queue is gone with it.
        if (Judge(peer, ToPeerRejection(enqueued.error())) == Verdict::kDisconnect) {
          return;
        }
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
      Judge(message.from, PeerRejection::kUndecodable);
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
      Judge(message.from, PeerRejection::kNotAClientMessage);
    }
  }

  // Counts rejection toward peer's misbehaviour and, once it has misbehaved too
  // often, disconnects it, as a departure like any other. Returns which.
  Verdict Judge(networking::PeerId peer, PeerRejection rejection) {
    const Verdict verdict = misbehaviour[peer].Record(rejection, std::chrono::steady_clock::now());
    if (verdict == Verdict::kDisconnect) {
      Expel(peer, rejection);
    }
    return verdict;
  }

  // Disconnects peer for misbehaving, the last time for rejection. The
  // transport reports no departure for a connection this side closes, so the
  // player leaves here.
  void Expel(networking::PeerId peer, PeerRejection rejection) {
    ++activity.misbehaving;
    if (const std::optional<SessionId> session = match.SessionOf(peer); session.has_value()) {
      LW("subsystem=serverruntime event=misbehaving_disconnected peer={} session={} reason=\"{}\"", PeerNumber(peer),
         SessionNumber(*session), DescribePeerRejection(rejection));
    } else {
      LW("subsystem=serverruntime event=misbehaving_disconnected peer={} reason=\"{}\"", PeerNumber(peer),
         DescribePeerRejection(rejection));
    }
    expelled.insert(peer);
    network.Disconnect(peer);
    HandleDisconnect(peer, Leaving::kMisbehaving);
  }

  // A player whose connection ended leaves at once; a body it had leaves the
  // simulation at the start of the next tick. how only decides what is logged.
  void HandleDisconnect(networking::PeerId peer, Leaving how) {
    misbehaviour.erase(peer);
    const std::optional<SessionId> session = match.SessionOf(peer);
    if (!session.has_value()) {
      return;
    }
    const Departure departure = match.Leave(peer);
    players.erase(*session);
    switch (departure) {
      case Departure::kNone:
        break;
      case Departure::kFromLobby:
        LI("subsystem=serverruntime event=lobby_left peer={} session={} how={} players={} version={}", PeerNumber(peer),
           SessionNumber(*session), LeavingName(how), match.PlayerCount(), match.GetRoster().version);
        SendRoster();
        break;
      case Departure::kFromMatch:
        LI("subsystem=serverruntime event=match_left peer={} session={} how={} playing={}", PeerNumber(peer),
           SessionNumber(*session), LeavingName(how), match.Playing().size());
        break;
      case Departure::kEndedMatch:
        LI("subsystem=serverruntime event=match_left peer={} session={} how={} playing=0", PeerNumber(peer),
           SessionNumber(*session), LeavingName(how));
        LogMatchEnded(EndReason::kNoPlayersLeft, std::nullopt, 0);
        break;
    }
  }

  // One line for every match that ends (ADR-0029): why, who won, how many
  // ticks it lasted, counting the one it ended on, and how many were in it.
  void LogMatchEnded(EndReason reason, const std::optional<SessionId>& winner, std::size_t playing) const {
    LI("subsystem=serverruntime event=match_ended reason=\"{}\" winner={} ticks={} players={} version={}",
       EndReasonName(reason), WinnerName(winner), tick - match_start_tick + 1, playing, match.GetRoster().version);
  }

  // Ends the match in progress, if any, with winner or as a draw, for reason:
  // its players are told, and are back in the Lobby; their bodies and the
  // bullets in flight leave the simulation at the start of the next tick.
  void EndMatch(const std::optional<SessionId>& winner, EndReason reason) {
    const std::optional<MatchEnd> ended = match.End(winner);
    if (!ended.has_value()) {
      return;
    }
    SendTo(ended->players, ToWire(*ended));
    LogMatchEnded(reason, ended->winner, ended->players.size());
    SendRoster();
  }

  // Acts on Game policy's Match end, after the tick it was decided on.
  void Act(const simulation::MatchEnd& end) {
    const std::lock_guard<std::mutex> lock(mutex);
    EndMatch(end.winner.transform([](simulation::SessionId winner) { return FromSimulation(winner); }),
             EndReason::kWinCondition);
  }

  // Starts a match if one can start: its players' bodies enter the simulation
  // at the Spawn points Game policy gives them, their commands start afresh,
  // and they are told where each spawned.
  void StartMatchIfReady() {
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
                                  .character = characters.at(player.character - 1)});
      bodies.emplace(player.session, player.entity);
      players.at(player.session).commands = CommandQueue{tick_rate_hz};
      sessions.push_back(player.session);
    }
    const std::vector<math::Vec3> spawns = simulation.StartMatch(entrants, spawn_points);
    simulating_match = true;
    // Its first tick is the one about to run.
    match_start_tick = tick + 1;
    SendTo(sessions, ToWire(*start, spawns));
    LI("subsystem=serverruntime event=match_started tick={} players={}", match_start_tick, sessions.size());
  }

  // What a tick runs on: one command per player in the match, and who to send
  // the tick's state to.
  struct TickInput {
    std::vector<simulation::PlayerCommand> commands;
    std::vector<replication::Recipient> recipients;
    // The connection of each recipient, by the entity its player controls.
    std::unordered_map<EntityId, networking::PeerId> peers;
  };

  // Takes a match that has ended out of the simulation, or the bodies of
  // players who left the one in progress, starts a match if one can start,
  // then takes one command per player in it for this tick.
  TickInput PrepareTick() {
    const std::lock_guard<std::mutex> lock(mutex);
    if (simulating_match && !match.InMatch()) {
      simulation.EndMatch();
      bodies.clear();
      simulating_match = false;
    }
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
       "messages={} stale={} dropped={} overflow={} misbehaving={}",
       tick.load(), players.size(), match.InMatch(), activity.ticks, activity.late, activity.overrun, activity.messages,
       activity.stale, activity.dropped, activity.overflow, activity.misbehaving);
    activity = Activity{};
    activity_since = now;
  }

  // One line per hit of the tick, then one per death. At INFO, though players
  // provoke them: a Match's hits and deaths are what its operator reads the log
  // for (US-12, US-13), in a Release build too.
  void LogCombat(const simulation::State& state) const {
    for (const simulation::Hit& hit : state.hits) {
      LI("subsystem=serverruntime event=hit tick={} shooter={} target={} part={} damage={} health={}", state.tick,
         std::to_underlying(hit.shooter), std::to_underlying(hit.target), BodyPartName(hit.part), hit.damage,
         hit.health);
    }
    for (const simulation::Death& death : state.deaths) {
      LI("subsystem=serverruntime event=death tick={} victim={} killer={} part={}", state.tick,
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
        impl.HandleDisconnect(event.peer, event.reason == networking::DisconnectReason::kConnectionLost
                                              ? Leaving::kTimedOut
                                              : Leaving::kLeft);
        break;
      }
    }
  }
  for (const networking::PeerMessage& message : impl.network.ReceiveMessages()) {
    LT("subsystem=serverruntime event=received peer={} bytes={}", PeerNumber(message.from), message.payload.size());
    const std::lock_guard<std::mutex> lock(impl.mutex);
    if (impl.expelled.contains(message.from)) {
      continue;
    }
    ++impl.activity.messages;
    impl.HandleMessage(message);
  }
  // A closed connection delivers nothing more.
  const std::lock_guard<std::mutex> lock(impl.mutex);
  impl.expelled.clear();
}

simulation::TickResult Host::Tick(float delta_time) {
  Impl& impl = *impl_;
  const Impl::TickInput input = impl.PrepareTick();
  const simulation::TickResult result = impl.simulation.Tick(input.commands, delta_time);
  impl.tick = result.state.tick;
  impl.Send(result.state, input);
  impl.LogCombat(result.state);
  // After the tick's own messages, so a client hears the deaths that ended the
  // match before it hears that it has.
  for (const simulation::PolicyAction& action : result.actions) {
    std::visit([&impl](const auto& typed) { impl.Act(typed); }, action);
  }
  return result;
}

void Host::RecordTiming(const tick::Timing& timing) { impl_->Heartbeat(timing); }

void Host::EndMatch() {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->EndMatch(std::nullopt, EndReason::kEndedByTheHost);
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
