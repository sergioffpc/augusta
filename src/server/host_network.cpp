#include <chrono>
#include <cstdint>
#include <expected>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "admission.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/protocol.h"
#include "command_queue.h"
#include "host.h"
#include "host_impl.h"
#include "host_log.h"
#include "match.h"
#include "misbehaviour.h"
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
    Reply(peer, protocol::JoinRefusedWire{.reason = ToWire(admission.error())});
    Judge(peer, PeerRejection::kJoinRefused, now);
    return;
  }
  admission_deadlines.Admitted(peer);
  Reply(peer, ToWire(*admission, tick_rate_hz, parameters));
  if (players.try_emplace(admission->session, Player{.peer = peer, .commands = CommandQueue{tick_rate_hz}}).second) {
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
    ++heartbeat.Current().dropped;
    LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped peer={} reason=\"commands before joining\"",
               PeerNumber(peer));
    Judge(peer, PeerRejection::kCommandsBeforeJoining, now);
    return;
  }
  if (!match.IsPlaying(*session)) {
    ++heartbeat.Current().stale;
    LT("subsystem=serverruntime event=dropped peer={} reason=\"commands outside a match\"", PeerNumber(peer));
    Judge(peer, PeerRejection::kCommandsOutsideMatch, now);
    return;
  }
  CommandQueue& queue = players.at(*session).commands;
  for (const SequencedCommand& command : commands) {
    const auto enqueued = queue.TryEnqueue(command);
    if (!enqueued.has_value()) {
      RecordRejection(peer, command, enqueued.error());
      // A disconnected player's queue is gone with it.
      if (Judge(peer, ToPeerRejection(enqueued.error()), now) == Verdict::kDisconnect) {
        return;
      }
    } else if (*enqueued == Enqueued::kDroppedOldest) {
      ++heartbeat.Current().overflow;
    }
  }
}

// Counts and logs a command the queue turned away.
void Host::Impl::RecordRejection(networking::PeerId peer, const SequencedCommand& command, Rejection rejection) {
  // Commands are repeated until acknowledged, so a stale one is routine.
  if (rejection == Rejection::kStale) {
    ++heartbeat.Current().stale;
    LT("subsystem=serverruntime event=dropped peer={} sequence={} reason=\"{}\"", PeerNumber(peer), command.sequence,
       DescribeRejection(rejection));
  } else {
    ++heartbeat.Current().dropped;
    LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped_malformed peer={} sequence={} reason=\"{}\"",
               PeerNumber(peer), command.sequence, DescribeRejection(rejection));
  }
}

void Host::Impl::HandleMessage(const networking::PeerMessage& message, std::chrono::steady_clock::time_point now) {
  const std::expected<protocol::MessageWire, protocol::DecodeError> decoded = protocol::Decode(message.payload);
  if (!decoded.has_value()) {
    ++heartbeat.Current().dropped;
    LW_LIMITED(drop_warnings, "subsystem=serverruntime event=dropped_malformed peer={} bytes={} reason=\"{}\"",
               PeerNumber(message.from), message.payload.size(), protocol::DescribeDecodeError(decoded.error()));
    Judge(message.from, PeerRejection::kUndecodable, now);
    return;
  }
  if (const auto* request = std::get_if<protocol::JoinRequestWire>(&*decoded)) {
    HandleJoinRequest(message.from, FromWire(*request), now);
  } else if (const auto* commands = std::get_if<protocol::CommandsWire>(&*decoded)) {
    HandleCommands(message.from, FromWire(*commands), now);
  } else if (const auto* ready = std::get_if<protocol::ReadyWire>(&*decoded)) {
    HandleReady(message.from, ready->version, now);
  } else {
    ++heartbeat.Current().dropped;
    LW_LIMITED(drop_warnings,
               "subsystem=serverruntime event=dropped_malformed peer={} bytes={} reason=\"not a client message\"",
               PeerNumber(message.from), message.payload.size());
    Judge(message.from, PeerRejection::kNotAClientMessage, now);
  }
}

// Counts rejection, at now, toward peer's misbehaviour and, once it has
// misbehaved too often, disconnects it, as a departure like any other.
// Returns which.
Verdict Host::Impl::Judge(networking::PeerId peer, PeerRejection rejection, std::chrono::steady_clock::time_point now) {
  const Verdict verdict = misbehaviour[peer].Record(rejection, now);
  if (verdict == Verdict::kDisconnect) {
    Expel(peer, DescribePeerRejection(rejection));
  }
  return verdict;
}

// Disconnects each peer whose admission deadline has passed at now.
void Host::Impl::ExpelUnadmitted(std::chrono::steady_clock::time_point now) {
  for (const networking::PeerId peer : admission_deadlines.TakeOverdue(now)) {
    Expel(peer, "not admitted in time");
  }
}

// Disconnects peer for misbehaving or for not being admitted in time, for
// reason, which only decides what is logged. The transport reports no
// departure for a connection this side closes, so the player leaves here.
void Host::Impl::Expel(networking::PeerId peer, std::string_view reason) {
  ++heartbeat.Current().misbehaving;
  if (const std::optional<SessionId> session = match.SessionOf(peer); session.has_value()) {
    LW("subsystem=serverruntime event=misbehaving_disconnected peer={} session={} reason=\"{}\"", PeerNumber(peer),
       SessionNumber(*session), reason);
  } else {
    LW("subsystem=serverruntime event=misbehaving_disconnected peer={} reason=\"{}\"", PeerNumber(peer), reason);
  }
  expelled.insert(peer);
  network.Disconnect(peer);
  HandleDisconnect(peer, Leaving::kMisbehaving);
}

// A player whose connection ended leaves at once; a body it had leaves the
// simulation at the start of the next tick. how only decides what is logged.
void Host::Impl::HandleDisconnect(networking::PeerId peer, Leaving how) {
  misbehaviour.erase(peer);
  admission_deadlines.Left(peer);
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

void Host::PumpNetwork(std::chrono::steady_clock::time_point now) {
  Impl& impl = *impl_;
  for (const networking::PeerEvent& event : impl.network.PumpEvents()) {
    switch (event.type) {
      case networking::PeerEventType::kConnectRequested: {
        // Every connection is accepted, since a refusal is a message and needs
        // the connection to travel on; whether the peer joins the Lobby is
        // decided by its JoinRequestWire, within kAdmissionDeadline.
        impl.network.Accept(event.peer);
        const std::lock_guard<std::mutex> lock(impl.mutex);
        impl.admission_deadlines.Connected(event.peer, now);
        break;
      }
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
    ++impl.heartbeat.Current().messages;
    impl.HandleMessage(message, now);
  }
  const std::lock_guard<std::mutex> lock(impl.mutex);
  // After the messages, so a Join that arrived in time is never too late.
  impl.ExpelUnadmitted(now);
  // A closed connection delivers nothing more.
  impl.expelled.clear();
}

}  // namespace augusta::server
