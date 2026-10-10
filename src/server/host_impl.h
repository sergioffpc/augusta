#ifndef AUGUSTA_SERVER_HOST_IMPL_H_
#define AUGUSTA_SERVER_HOST_IMPL_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "admission.h"
#include "augusta/failure.h"
#include "augusta/first_failure.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "capture.h"
#include "command_queue.h"
#include "heartbeat.h"
#include "host.h"
#include "host_log.h"
#include "host_metrics.h"
#include "match.h"
#include "misbehaviour.h"
#include "peer_gate.h"
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

  // Written in place by both threads, lock-free; read by the metrics endpoint's.
  // Declared before the recording, which writes its state here until it is
  // closed.
  HostMetrics metrics;
  // Declared before the socket so it is constructed first; see
  // BuildRecordedSimulation. Simulation thread only, with the file it records
  // to, if any, the body each player in it controls, and whether the match
  // those bodies are in is still in the simulation.
  std::ofstream recording_file;
  RecordedSimulation simulation;
  // What the capturer tells of itself, counted into metrics; declared before
  // it, so it outlives the capturer's writer.
  CaptureMetrics capture_metrics{metrics};
  // Each Match's capture, if HostConfig::capture_directory asks for them (ADR-0050).
  std::unique_ptr<Capturer> capturer;
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
  // The first local transport failure either thread met, until a worker takes it.
  failure::FirstFailure transport_failure;
  // The first outbound message or record either thread could not encode: a
  // broken invariant, never sent, until a worker takes it (ADR-0033).
  failure::FirstFailure invariant_failure;
  // Whether either failure has been recorded, taken or not: the runtime has
  // failed, and the Host sends nothing more until its workers stop.
  [[nodiscard]] bool Failed() const;

  // Guards everything below: written by the Network I/O thread as clients
  // join, leave and send commands, and by the Simulation thread as matches
  // start and end.
  std::mutex mutex;
  Match match;
  std::unordered_map<SessionId, Player> players;
  // The tick the match in progress, or the last one, started on.
  tick::Tick match_start_tick = 0;
  // The winner of the match that ended last, nullopt for a draw: what its
  // capture's Match end names once the Simulation thread takes it out.
  std::optional<SessionId> match_winner;
  // When the next heartbeat line is due, and the totals the last one ended at.
  // Simulation thread only.
  Heartbeat heartbeat{std::chrono::steady_clock::now()};
  // A peer can send malformed messages as fast as it likes, so their warnings
  // are limited; the heartbeat still counts every one.
  logging::Throttle drop_warnings{std::chrono::seconds{1}};
  // Each connected peer's admission deadline and misbehaviour, and the peers
  // expelled during this PumpNetwork, whose messages still in its batch are
  // ignored (peer_gate.h).
  PeerGate gate{metrics};

  Impl(const HostConfig& config, Scenario scenario, scripting::Engine policy);

  // Sending to players (host.cpp), each message as EncodeToSend left it
  // (wire.h), reliably, counted as the transport accepts it (SendCounted).
  // One the protocol could not carry is sent to no one and kept in
  // invariant_failure; a local transport failure is kept in transport_failure.
  // Once either is, the runtime has failed (Failed), and nothing more is sent.
  void Reply(networking::PeerId peer, const std::expected<networking::Payload, failure::Failure>& message);
  // Sends payload, already encoded, to peer reliably, as Reply does.
  void Deliver(networking::PeerId peer, const networking::Payload& payload);
  // Sends message reliably to the player of each of sessions.
  void SendTo(const std::vector<SessionId>& sessions,
              const std::expected<networking::Payload, failure::Failure>& message);
  // Tells everyone in the Lobby who is in it, after it changed.
  void SendRoster();
  // Sets the metrics' gauges of who is joined, in the Lobby and in a match,
  // after any of it changed. With mutex held.
  void SetLobbyGauges();

  // The Network I/O thread's side (host_network.cpp), with mutex held.
  void HandleMessage(const networking::PeerMessage& message, std::chrono::steady_clock::time_point now);
  void HandleJoinRequest(networking::PeerId peer, const JoinRequest& request,
                         std::chrono::steady_clock::time_point now);
  void HandleReady(networking::PeerId peer, std::uint32_t version, std::chrono::steady_clock::time_point now);
  void HandleCommands(networking::PeerId peer, const std::vector<SequencedCommand>& commands,
                      std::chrono::steady_clock::time_point now);
  void RecordRejection(networking::PeerId peer, const SequencedCommand& command, Rejection rejection);
  Verdict Judge(networking::PeerId peer, PeerRejection rejection, std::chrono::steady_clock::time_point now);
  void RefuseAsNoReplayServer(networking::PeerId peer, std::chrono::steady_clock::time_point now);
  void Expel(networking::PeerId peer, std::string_view reason);
  void HandleDisconnect(networking::PeerId peer, Leaving how);

  // The Simulation thread's side (host_match.cpp). CountMatchEnded, EndMatch
  // and StartMatchIfReady with mutex held; Act and PrepareTick take it.
  // How many ticks the match in progress, or the last one, has lasted,
  // counting the one it ended on.
  [[nodiscard]] tick::Tick MatchTicks() const { return tick - match_start_tick + 1; }
  void LogMatchEnded(EndReason reason, const std::optional<SessionId>& winner, std::size_t playing) const;
  void CountMatchEnded(EndReason reason, const std::optional<SessionId>& winner);
  void EndMatch(const std::optional<SessionId>& winner, EndReason reason);
  void Act(const simulation::MatchEnd& end);
  void StartMatchIfReady();
  void TakeOutEndedMatch();
  TickInput PrepareTick();
  void CaptureDeaths(const simulation::TickResult& result) const;
  // Sends the tick of state's messages to, unless the runtime has failed.
  void SendTick(const simulation::State& state, const TickRecipients& to);
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HOST_IMPL_H_
