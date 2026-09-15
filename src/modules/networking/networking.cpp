#include "augusta/networking.h"

namespace augusta::networking {

// TODO(sergioffpc): every function below is a placeholder -
// GameNetworkingSockets isn't wired in yet (ADR-0003). Just enough is
// defined here for callers to construct/link against this module.

void Init() {
  // TODO(sergioffpc): one-time GameNetworkingSockets library init.
}

// ---- Client ----

Client::Client() {
  // Nothing to own yet - no connection exists until Connect is called.
}

void Client::Connect([[maybe_unused]] const Endpoint& server) {
  // TODO(sergioffpc): begin an async GameNetworkingSockets connection.
}

void Client::Disconnect() {
  // TODO(sergioffpc): close the connection, if any.
}

void Client::PumpEvents() {
  // TODO(sergioffpc): drain connection-lifecycle events, update state.
}

ConnectionState Client::GetState() const {
  // TODO(sergioffpc): report the real handshake/connection state.
  return ConnectionState::kDisconnected;
}

void Client::Send([[maybe_unused]] const Payload& payload) {
  // TODO(sergioffpc): send payload unreliably to the server.
}

std::vector<Payload> Client::ReceiveMessages() {
  // TODO(sergioffpc): return messages received since the last call.
  return {};
}

// ---- Server ----

Server::Server([[maybe_unused]] const Endpoint& local_endpoint) {
  // TODO(sergioffpc): start listening on local_endpoint.
}

std::vector<PeerEvent> Server::PumpEvents() {
  // TODO(sergioffpc): drain connection-lifecycle events since the last
  // call.
  return {};
}

void Server::Accept([[maybe_unused]] PeerId peer) {
  // TODO(sergioffpc): admit a pending peer.
}

void Server::Disconnect([[maybe_unused]] PeerId peer) {
  // TODO(sergioffpc): reject or drop peer's connection.
}

void Server::Send([[maybe_unused]] PeerId peer, [[maybe_unused]] const Payload& payload) {
  // TODO(sergioffpc): send payload to one connected peer.
}

void Server::Broadcast([[maybe_unused]] const Payload& payload) {
  // TODO(sergioffpc): send payload to every connected peer.
}

std::vector<PeerMessage> Server::ReceiveMessages() {
  // TODO(sergioffpc): return messages received from any peer since the
  // last call.
  return {};
}

}  // namespace augusta::networking
