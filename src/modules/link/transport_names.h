#ifndef AUGUSTA_LINK_TRANSPORT_NAMES_H_
#define AUGUSTA_LINK_TRANSPORT_NAMES_H_

#include <expected>
#include <utility>

#include "augusta/link.h"
#include "augusta/networking.h"

/// \file
/// The transport's names for a connection's state, a peer, its events and a
/// send's outcome in the Link's own, and back: private to the module, shared by
/// its ends (link.cpp) and its raw ends for tests (raw_link.cpp).
namespace augusta::link {

constexpr ConnectionState ToLink(networking::ConnectionState state) {
  switch (state) {
    case networking::ConnectionState::kConnecting:
      return ConnectionState::kConnecting;
    case networking::ConnectionState::kConnected:
      return ConnectionState::kConnected;
    case networking::ConnectionState::kDisconnected:
      return ConnectionState::kDisconnected;
  }
  // The transport has no other state.
  return ConnectionState::kDisconnected;
}

constexpr PeerEventType ToLink(networking::PeerEventType type) {
  switch (type) {
    case networking::PeerEventType::kConnectRequested:
      return PeerEventType::kConnectRequested;
    case networking::PeerEventType::kConnected:
      return PeerEventType::kConnected;
    case networking::PeerEventType::kDisconnected:
      return PeerEventType::kDisconnected;
  }
  // The transport has no other event.
  return PeerEventType::kDisconnected;
}

constexpr DisconnectReason ToLink(networking::DisconnectReason reason) {
  return reason == networking::DisconnectReason::kConnectionLost ? DisconnectReason::kConnectionLost
                                                                 : DisconnectReason::kClosedByPeer;
}

/// The Link names a peer by the number the transport gives its connection.
constexpr PeerId ToLink(networking::PeerId peer) { return PeerId{std::to_underlying(peer)}; }
constexpr networking::PeerId ToTransport(PeerId peer) { return networking::PeerId{std::to_underlying(peer)}; }

constexpr PeerEvent ToLink(const networking::PeerEvent& event) {
  return PeerEvent{.peer = ToLink(event.peer), .type = ToLink(event.type), .reason = ToLink(event.reason)};
}

/// What the transport did with a send, or its failure, as the Link says it.
inline SendResult ToLink(networking::SendResult sent) {
  if (!sent.has_value()) {
    return std::unexpected(std::move(sent.error()));
  }
  return *sent == networking::SendOutcome::kAccepted ? SendOutcome::kAccepted : SendOutcome::kDropped;
}

}  // namespace augusta::link

#endif  // AUGUSTA_LINK_TRANSPORT_NAMES_H_
