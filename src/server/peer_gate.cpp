#include "peer_gate.h"

#include <chrono>
#include <mutex>
#include <utility>
#include <vector>

#include "augusta/first_failure.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "host_log.h"
#include "host_metrics.h"
#include "misbehaviour.h"

namespace augusta::server {

void PeerGate::Connected(networking::PeerId peer, std::chrono::steady_clock::time_point now) {
  admission_deadlines_.Connected(peer, now);
}

void PeerGate::Admitted(networking::PeerId peer) { admission_deadlines_.Admitted(peer); }

void PeerGate::Left(networking::PeerId peer) {
  misbehaviour_.erase(peer);
  admission_deadlines_.Left(peer);
}

Verdict PeerGate::Judge(networking::PeerId peer, PeerRejection rejection, std::chrono::steady_clock::time_point now) {
  if (IsMisbehaviour(rejection)) {
    metrics_.misbehaviour[rejection].Increment();
  }
  return misbehaviour_[peer].Record(rejection, now);
}

void PeerGate::Expelled(networking::PeerId peer) { expelled_.insert(peer); }

bool PeerGate::WasExpelled(networking::PeerId peer) const { return expelled_.contains(peer); }

std::vector<networking::PeerId> PeerGate::TakeOverdue(std::chrono::steady_clock::time_point now) {
  return admission_deadlines_.TakeOverdue(now);
}

void PeerGate::EndRound() { expelled_.clear(); }

void PumpPeers(networking::Server& network, HostMetrics& metrics, std::mutex& mutex, PeerGate& gate,
               std::chrono::steady_clock::time_point now, const PeerHandlers& handlers,
               failure::FirstFailure& transport_failure) {
  for (const networking::PeerEvent& event : network.PumpEvents()) {
    switch (event.type) {
      case networking::PeerEventType::kConnectRequested: {
        // Every connection is accepted, since a refusal is a message and needs
        // the connection to travel on; whether the peer is admitted is decided
        // by what it asks first, within kAdmissionDeadline.
        network.Accept(event.peer);
        const std::lock_guard<std::mutex> lock(mutex);
        gate.Connected(event.peer, now);
        break;
      }
      case networking::PeerEventType::kConnected:
        break;
      case networking::PeerEventType::kDisconnected: {
        const std::lock_guard<std::mutex> lock(mutex);
        handlers.disconnected(event.peer, event.reason == networking::DisconnectReason::kConnectionLost
                                              ? Leaving::kTimedOut
                                              : Leaving::kLeft);
        break;
      }
    }
  }
  auto received = network.ReceiveMessages();
  if (!received.has_value()) {
    // Nothing more this round: the runtime stops on it.
    transport_failure.Record(std::move(received.error()));
    return;
  }
  for (const networking::PeerMessage& message : *received) {
    LT("subsystem=serverruntime event=received peer={} bytes={}", PeerNumber(message.from), message.payload.size());
    std::unique_lock<std::mutex> lock(mutex);
    if (gate.WasExpelled(message.from)) {
      continue;
    }
    metrics.received_bytes.Increment(message.payload.size());
    handlers.message(message, lock);
  }
  const std::lock_guard<std::mutex> lock(mutex);
  // After the messages, so a request that arrived in time is never too late.
  for (const networking::PeerId peer : gate.TakeOverdue(now)) {
    handlers.overdue(peer);
  }
  gate.EndRound();
}

}  // namespace augusta::server
