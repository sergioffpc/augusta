#include <chrono>
#include <cstdint>
#include <expected>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/protocol.h"
#include "command_queue.h"
#include "connection_sample.h"
#include "host.h"
#include "host_impl.h"
#include "host_log.h"
#include "host_metrics.h"
#include "match.h"
#include "misbehaviour.h"
#include "peer_gate.h"
#include "wire.h"

namespace augusta::server {

namespace {

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

}  // namespace

void Host::Impl::HandleJoinRequest(networking::PeerId peer, const JoinRequest& request,
                                   std::chrono::steady_clock::time_point now) {
  const auto admission = match.Join(peer, request);
  if (!admission.has_value()) {
    LI("subsystem=serverruntime event=join_refused peer={} reason=\"{}\"", PeerNumber(peer),
       DescribeJoinRefusal(admission.error()));
    Reply(peer, EncodeToSend(protocol::JoinRefusedWire{.reason = ToWire(admission.error())}));
    metrics.joins_refused[admission.error()].Increment();
    Judge(peer, PeerRejection::kJoinRefused, now);
    return;
  }
  gate.Admitted(peer);
  Reply(peer, EncodeToSend(ToWire(*admission, tick_rate_hz, parameters)));
  if (players.try_emplace(admission->session, Player{.peer = peer, .commands = CommandQueue{tick_rate_hz}}).second) {
    metrics.joins_admitted.Increment();
    SetLobbyGauges();
    LI("subsystem=serverruntime event=lobby_joined peer={} session={} character={} players={} version={}",
       PeerNumber(peer), SessionNumber(admission->session), admission->character, match.PlayerCount(),
       match.GetRoster().version);
    SendRoster();
  }
}

void Host::Impl::HandleReady(networking::PeerId peer, std::uint32_t version,
                             std::chrono::steady_clock::time_point now) {
  if (match.Ready(peer, version)) {
    LI("subsystem=serverruntime event=ready peer={} version={}", PeerNumber(peer), version);
  } else {
    // The Roster can change while a Ready is in flight; the client sends another for the new one.
    LD("subsystem=serverruntime event=ready_ignored peer={} version={} current={}", PeerNumber(peer), version,
       match.GetRoster().version);
    Judge(peer, PeerRejection::kStaleReady, now);
  }
}

void Host::Impl::HandleCommands(networking::PeerId peer, const std::vector<SequencedCommand>& commands,
                                std::chrono::steady_clock::time_point now) {
  const std::optional<SessionId> session = match.SessionOf(peer);
  if (!session.has_value()) {
    metrics.commands_received.Increment(commands.size());
    metrics.commands_before_joining.Increment(commands.size());
    LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped peer={} reason=\"commands before joining\"",
               PeerNumber(peer));
    Judge(peer, PeerRejection::kCommandsBeforeJoining, now);
    return;
  }
  if (!match.IsPlaying(*session)) {
    metrics.commands_received.Increment(commands.size());
    metrics.commands_outside_match.Increment(commands.size());
    LT("subsystem=serverruntime event=dropped peer={} reason=\"commands outside a match\"", PeerNumber(peer));
    Judge(peer, PeerRejection::kCommandsOutsideMatch, now);
    return;
  }
  CommandQueue& queue = players.at(*session).commands;
  for (const SequencedCommand& command : commands) {
    // One at a time: those after a command that disconnects the peer are never taken in.
    metrics.commands_received.Increment();
    const auto enqueued = queue.TryEnqueue(command);
    if (!enqueued.has_value()) {
      RecordRejection(peer, command, enqueued.error());
      // A disconnected player's queue is gone with it.
      if (Judge(peer, ToPeerRejection(enqueued.error()), now) == Verdict::kDisconnect) {
        return;
      }
    } else if (*enqueued == Enqueued::kDroppedOldest) {
      metrics.commands_overflowed.Increment();
    }
  }
}

// Counts and logs a command the queue turned away.
void Host::Impl::RecordRejection(networking::PeerId peer, const SequencedCommand& command, Rejection rejection) {
  metrics.commands_rejected[rejection].Increment();
  // Commands are repeated until acknowledged, so a stale one is routine.
  if (rejection == Rejection::kStale) {
    LT("subsystem=serverruntime event=dropped peer={} sequence={} reason=\"{}\"", PeerNumber(peer), command.sequence,
       DescribeRejection(rejection));
  } else {
    LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped_malformed peer={} sequence={} reason=\"{}\"",
               PeerNumber(peer), command.sequence, DescribeRejection(rejection));
  }
}

void Host::Impl::HandleMessage(const networking::PeerMessage& message, std::chrono::steady_clock::time_point now) {
  const std::expected<protocol::MessageWire, protocol::DecodeError> decoded = protocol::Decode(message.payload);
  if (!decoded.has_value()) {
    LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped_malformed peer={} bytes={} reason=\"{}\"",
               PeerNumber(message.from), message.payload.size(), protocol::DescribeDecodeError(decoded.error()));
    Judge(message.from, PeerRejection::kUndecodable, now);
    return;
  }
  metrics.messages_received[TypeOf(message.payload)].Increment();
  if (const auto* request = std::get_if<protocol::JoinRequestWire>(&*decoded)) {
    HandleJoinRequest(message.from, FromWire(*request), now);
  } else if (const auto* commands = std::get_if<protocol::CommandsWire>(&*decoded)) {
    HandleCommands(message.from, FromWire(*commands), now);
  } else if (const auto* ready = std::get_if<protocol::ReadyWire>(&*decoded)) {
    HandleReady(message.from, ready->version, now);
  } else if (std::holds_alternative<protocol::ReplayListRequestWire>(*decoded) ||
             std::holds_alternative<protocol::ReplayRequestWire>(*decoded)) {
    RefuseAsNoReplayServer(message.from, now);
  } else {
    LW_LIMITED(drop_warnings,
               "subsystem=serverruntime event=dropped_malformed peer={} bytes={} reason=\"not a client message\"",
               PeerNumber(message.from), message.payload.size());
    Judge(message.from, PeerRejection::kNotAClientMessage, now);
  }
}

// A live server replays nothing (ADR-0051): a Replay list request or a
// Replay request is told so, so `augustac --replays` or `--replay` against it
// says what is wrong rather than waiting.
void Host::Impl::RefuseAsNoReplayServer(networking::PeerId peer, std::chrono::steady_clock::time_point now) {
  constexpr JoinRefusal kReason = JoinRefusal::kNotAReplayServer;
  LI("subsystem=serverruntime event=join_refused peer={} reason=\"{}\"", PeerNumber(peer),
     DescribeJoinRefusal(kReason));
  Reply(peer, EncodeToSend(protocol::JoinRefusedWire{.reason = ToWire(kReason)}));
  metrics.joins_refused[kReason].Increment();
  Judge(peer, PeerRejection::kJoinRefused, now);
}

// Counts rejection, at now, toward peer's misbehaviour and, once it has
// misbehaved too often, disconnects it, as a departure like any other.
// Returns which.
Verdict Host::Impl::Judge(networking::PeerId peer, PeerRejection rejection, std::chrono::steady_clock::time_point now) {
  const Verdict verdict = gate.Judge(peer, rejection, now);
  if (verdict == Verdict::kDisconnect) {
    Expel(peer, DescribePeerRejection(rejection));
  }
  return verdict;
}

// Disconnects peer for misbehaving or for not being admitted in time, for
// reason, which only decides what is logged. The transport reports no
// departure for a connection this side closes, so the player leaves here.
void Host::Impl::Expel(networking::PeerId peer, std::string_view reason) {
  if (const std::optional<SessionId> session = match.SessionOf(peer); session.has_value()) {
    LW("subsystem=serverruntime event=misbehaving_disconnected peer={} session={} reason=\"{}\"", PeerNumber(peer),
       SessionNumber(*session), reason);
  } else {
    LW("subsystem=serverruntime event=misbehaving_disconnected peer={} reason=\"{}\"", PeerNumber(peer), reason);
  }
  gate.Expelled(peer);
  network.Disconnect(peer);
  HandleDisconnect(peer, Leaving::kMisbehaving);
}

// A player whose connection ended leaves at once; a body it had leaves the
// simulation at the start of the next tick. how only decides what is logged.
void Host::Impl::HandleDisconnect(networking::PeerId peer, Leaving how) {
  gate.Left(peer);
  const std::optional<SessionId> session = match.SessionOf(peer);
  if (!session.has_value()) {
    metrics.disconnects_before_admission[how].Increment();
    return;
  }
  const Departure departure = match.Leave(peer);
  players.erase(*session);
  SetLobbyGauges();
  switch (departure) {
    case Departure::kNone:
      break;
    case Departure::kFromLobby:
      metrics.disconnects_from_lobby[how].Increment();
      LI("subsystem=serverruntime event=lobby_left peer={} session={} how={} players={} version={}", PeerNumber(peer),
         SessionNumber(*session), LeavingName(how), match.PlayerCount(), match.GetRoster().version);
      SendRoster();
      break;
    case Departure::kFromMatch:
      metrics.disconnects_from_match[how].Increment();
      LI("subsystem=serverruntime event=match_left peer={} session={} how={} playing={}", PeerNumber(peer),
         SessionNumber(*session), LeavingName(how), match.Playing().size());
      break;
    case Departure::kEndedMatch:
      metrics.disconnects_from_match[how].Increment();
      CountMatchEnded(EndReason::kNoPlayersLeft, std::nullopt);
      LI("subsystem=serverruntime event=match_left peer={} session={} how={} playing=0", PeerNumber(peer),
         SessionNumber(*session), LeavingName(how));
      LogMatchEnded(EndReason::kNoPlayersLeft, std::nullopt, 0);
      break;
  }
}

void Host::PumpNetwork(std::chrono::steady_clock::time_point now) {
  Impl& impl = *impl_;
  PumpPeers(impl.network, impl.metrics, impl.mutex, impl.gate, now,
            PeerHandlers{
                .disconnected = [&impl](networking::PeerId peer, Leaving how) { impl.HandleDisconnect(peer, how); },
                .message = [&impl, now](const networking::PeerMessage& message,
                                        std::unique_lock<std::mutex>& /*lock*/) { impl.HandleMessage(message, now); },
                .overdue = [&impl](networking::PeerId peer) { impl.Expel(peer, "not admitted in time"); },
            },
            impl.transport_failure);
}

std::vector<ConnectionSample> Host::SampleConnections() {
  const std::vector<networking::PeerStats> measured = impl_->network.GetStats();
  std::vector<ConnectionSample> samples;
  samples.reserve(measured.size());
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  for (const networking::PeerStats& peer : measured) {
    samples.push_back(ConnectionSample{.session = impl_->match.SessionOf(peer.peer), .stats = peer.stats});
  }
  return samples;
}

}  // namespace augusta::server
