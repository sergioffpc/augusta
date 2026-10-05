#ifndef AUGUSTA_SERVER_HOST_IMPL_H_
#define AUGUSTA_SERVER_HOST_IMPL_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "admission.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "command_queue.h"
#include "heartbeat.h"
#include "host.h"
#include "host_log.h"
#include "match.h"
#include "misbehaviour.h"
#include "recording.h"
#include "tick_messages.h"

/// \file
/// Host's state, shared by the files that each implement one side of it:
/// host.cpp builds it and sends to players, host_network.cpp is the Network I/O
/// thread's side (connections, messages, misbehaving peers) and host_match.cpp
/// the Simulation thread's (ticks, and matches starting and ending). Private to
/// Host: nothing else includes it.
namespace augusta::server {

struct Host::Impl {
  // What the server keeps per joined client.
  struct Player {
    networking::PeerId peer;
    CommandQueue commands;
  };

  // What a tick runs on: one command per player in the match, and who to send
  // the tick's state to.
  struct TickInput {
    std::vector<simulation::PlayerCommand> commands;
    TickRecipients to;
  };

  // Declared before the socket so it is constructed first; see
  // BuildRecordedSimulation. Simulation thread only, with the file it records
  // to, if any, the body each player in it controls, and whether the match
  // those bodies are in is still in the simulation.
  std::ofstream recording_file;
  RecordedSimulation simulation;
  std::unordered_map<SessionId, EntityId> bodies;
  bool simulating_match = false;
  // The last tick SimulationWorld ran, as it numbers them: what its State was
  // sent under, and what a client names the Seen time of its Commands by. Written by
  // the Simulation thread; read by the Network I/O thread too, to log how long
  // a match its last player left lasted.
  std::atomic<tick::Tick> tick = 0;
  // What every client is told when it joins, with the tick rate; neither ever
  // changes, so neither needs the lock.
  const std::uint8_t tick_rate_hz;
  const parameters::Parameters parameters;
  // The scenario's characters as SimulationWorld takes them, by path
  // (ADR-0042), and the Map's Spawn points; neither changes either.
  const std::unordered_map<std::string, simulation::Character> characters;
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
  // What Network I/O and the ticks did since the last heartbeat line.
  Heartbeat heartbeat{std::chrono::steady_clock::now()};
  // A peer can send malformed messages as fast as it likes, so their warnings
  // are limited; the heartbeat still counts every one.
  logging::Throttle drop_warnings{std::chrono::seconds{1}};
  // Each connected peer's misbehaviour, and the peers disconnected for it during
  // this PumpNetwork, whose messages still in its batch are ignored.
  std::unordered_map<networking::PeerId, MisbehaviourTracker> misbehaviour;
  std::unordered_set<networking::PeerId> expelled;
  // The deadline of each connected peer not yet admitted to the Lobby.
  AdmissionDeadlines admission_deadlines;

  Impl(const HostConfig& config, Scenario scenario, scripting::Engine policy);

  // Sending to players (host.cpp), each message already encoded (wire.h).
  void Reply(networking::PeerId peer, const networking::Payload& message);
  // Sends message reliably to the player of each of sessions.
  void SendTo(const std::vector<SessionId>& sessions, const networking::Payload& message);
  // Tells everyone in the Lobby who is in it, after it changed.
  void SendRoster();

  // The Network I/O thread's side (host_network.cpp), with mutex held.
  void HandleMessage(const networking::PeerMessage& message, std::chrono::steady_clock::time_point now);
  void HandleJoinRequest(networking::PeerId peer, const JoinRequest& request,
                         std::chrono::steady_clock::time_point now);
  void HandleReady(networking::PeerId peer, std::uint32_t version, std::chrono::steady_clock::time_point now);
  void HandleCommands(networking::PeerId peer, const std::vector<SequencedCommand>& commands,
                      std::chrono::steady_clock::time_point now);
  void RecordRejection(networking::PeerId peer, const SequencedCommand& command, Rejection rejection);
  Verdict Judge(networking::PeerId peer, PeerRejection rejection, std::chrono::steady_clock::time_point now);
  void ExpelUnadmitted(std::chrono::steady_clock::time_point now);
  void Expel(networking::PeerId peer, std::string_view reason);
  void HandleDisconnect(networking::PeerId peer, Leaving how);

  // The Simulation thread's side (host_match.cpp). EndMatch and
  // StartMatchIfReady with mutex held; Act and PrepareTick take it.
  void LogMatchEnded(EndReason reason, const std::optional<SessionId>& winner, std::size_t playing) const;
  void EndMatch(const std::optional<SessionId>& winner, EndReason reason);
  void Act(const simulation::MatchEnd& end);
  void StartMatchIfReady();
  TickInput PrepareTick();
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HOST_IMPL_H_
