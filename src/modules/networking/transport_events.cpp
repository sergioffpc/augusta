#include "transport_events.h"

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include "augusta/networking.h"

namespace augusta::networking {

namespace {

bool HasEnded(TransportState state) {
  return state == TransportState::kClosedByPeer || state == TransportState::kProblemDetectedLocally;
}

}  // namespace

void TransportEventQueue::Publish(TransportEvent event) {
  const std::lock_guard<std::mutex> lock(mutex_);
  events_.push_back(std::move(event));
}

std::vector<TransportEvent> TransportEventQueue::Drain() {
  const std::lock_guard<std::mutex> lock(mutex_);
  return std::exchange(events_, {});
}

TransportCall PeerTable::Apply(const TransportEvent& event) {
  const auto peer = static_cast<PeerId>(event.connection);
  switch (event.state) {
    case TransportState::kConnecting:
      pending_.insert(event.connection);
      return TransportCall::kNone;
    case TransportState::kConnected:
      pending_.erase(event.connection);
      connected_.insert(event.connection);
      queued_.push_back(PeerEvent{.peer = peer, .type = PeerEventType::kConnected});
      return TransportCall::kJoinPollGroup;
    case TransportState::kClosedByPeer:
    case TransportState::kProblemDetectedLocally:
      Forget(event.connection);
      queued_.push_back(PeerEvent{
          .peer = peer,
          .type = PeerEventType::kDisconnected,
          .reason = event.state == TransportState::kProblemDetectedLocally ? DisconnectReason::kConnectionLost
                                                                           : DisconnectReason::kClosedByPeer,
      });
      return TransportCall::kClose;
    case TransportState::kOther:
      return TransportCall::kNone;
  }
  return TransportCall::kNone;
}

std::vector<PeerEvent> PeerTable::TakeEvents() {
  std::vector<PeerEvent> events = std::exchange(queued_, {});
  for (const std::uint32_t connection : pending_) {
    events.push_back(PeerEvent{.peer = static_cast<PeerId>(connection), .type = PeerEventType::kConnectRequested});
  }
  return events;
}

void PeerTable::Answer(std::uint32_t connection) { pending_.erase(connection); }

void PeerTable::Forget(std::uint32_t connection) {
  pending_.erase(connection);
  connected_.erase(connection);
}

bool PeerTable::IsConnected(std::uint32_t connection) const { return connected_.contains(connection); }

std::vector<std::uint32_t> PeerTable::Connected() const { return {connected_.begin(), connected_.end()}; }

std::vector<std::uint32_t> PeerTable::Connections() const {
  std::vector<std::uint32_t> connections(pending_.begin(), pending_.end());
  connections.insert(connections.end(), connected_.begin(), connected_.end());
  return connections;
}

ClientTransition ApplyToClient(std::uint32_t current, ConnectionState state, const TransportEvent& event) {
  if (event.connection != current) {
    return ClientTransition{.state = state,
                            .call = HasEnded(event.state) ? TransportCall::kClose : TransportCall::kNone};
  }
  switch (event.state) {
    case TransportState::kConnecting:
      return ClientTransition{.state = ConnectionState::kConnecting};
    case TransportState::kConnected:
      return ClientTransition{.state = ConnectionState::kConnected};
    case TransportState::kClosedByPeer:
    case TransportState::kProblemDetectedLocally:
      return ClientTransition{.state = ConnectionState::kDisconnected, .call = TransportCall::kClose, .ended = true};
    case TransportState::kOther:
      break;
  }
  return ClientTransition{.state = state};
}

}  // namespace augusta::networking
