#include "augusta/peer_gate.h"

#include <chrono>
#include <mutex>
#include <utility>
#include <vector>

#include "augusta/first_failure.h"
#include "augusta/host_log.h"
#include "augusta/host_metrics.h"
#include "augusta/link.h"
#include "augusta/logging.h"
#include "augusta/misbehaviour.h"

namespace augusta::host {

void PeerGate::Connected(link::PeerId peer, std::chrono::steady_clock::time_point now) {
  admission_deadlines_.Connected(peer, now);
}

void PeerGate::Admitted(link::PeerId peer) { admission_deadlines_.Admitted(peer); }

void PeerGate::Left(link::PeerId peer) {
  misbehaviour_.erase(peer);
  admission_deadlines_.Left(peer);
}

Verdict PeerGate::Judge(link::PeerId peer, PeerRejection rejection, std::chrono::steady_clock::time_point now) {
  if (IsMisbehaviour(rejection)) {
    metrics_.misbehaviour[rejection].Increment();
  }
  return misbehaviour_[peer].Record(rejection, now);
}

void PeerGate::Expelled(link::PeerId peer) { expelled_.insert(peer); }

bool PeerGate::WasExpelled(link::PeerId peer) const { return expelled_.contains(peer); }

std::vector<link::PeerId> PeerGate::TakeOverdue(std::chrono::steady_clock::time_point now) {
  return admission_deadlines_.TakeOverdue(now);
}

void PeerGate::EndRound() { expelled_.clear(); }

void PumpPeers(link::Server& network, HostMetrics& metrics, std::mutex& mutex, PeerGate& gate,
               std::chrono::steady_clock::time_point now, const PeerHandlers& handlers,
               failure::FirstFailure& transport_failure) {
  for (const link::PeerEvent& event : network.PumpEvents()) {
    switch (event.type) {
      case link::PeerEventType::kConnectRequested: {
        // Every connection is accepted, since a refusal is a message and needs
        // the connection to travel on; whether the peer is admitted is decided
        // by what it asks first, within kAdmissionDeadline.
        network.Accept(event.peer);
        const std::lock_guard<std::mutex> lock(mutex);
        gate.Connected(event.peer, now);
        break;
      }
      case link::PeerEventType::kConnected:
        break;
      case link::PeerEventType::kDisconnected: {
        const std::lock_guard<std::mutex> lock(mutex);
        handlers.disconnected(
            event.peer, event.reason == link::DisconnectReason::kConnectionLost ? Leaving::kTimedOut : Leaving::kLeft);
        break;
      }
    }
  }
  auto received = network.Receive();
  if (!received.has_value()) {
    // Nothing more this round: the runtime stops on it.
    transport_failure.Record(std::move(received.error()));
    return;
  }
  for (const link::PeerMessage& message : *received) {
    LOG_TRACE("subsystem=serverruntime event=received peer={} bytes={}", PeerNumber(message.from), message.bytes);
    std::unique_lock<std::mutex> lock(mutex);
    if (gate.WasExpelled(message.from)) {
      continue;
    }
    metrics.received_bytes.Increment(message.bytes);
    handlers.message(message, lock);
  }
  const std::lock_guard<std::mutex> lock(mutex);
  // After the messages, so a request that arrived in time is never too late.
  for (const link::PeerId peer : gate.TakeOverdue(now)) {
    handlers.overdue(peer);
  }
  gate.EndRound();
}

}  // namespace augusta::host
