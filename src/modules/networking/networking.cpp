#include "augusta/networking.h"

#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>  // SteamNetworkingIPAddr::ParseString's inline body lives here.
#include <steam/steamnetworkingsockets.h>

#include <array>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <vector>

// M1 spike (ADR-0003): the first real (non-stub) body for this module.
// Both Client and Server route GameNetworkingSockets' single global
// connection-status-changed callback back to their own Impl via
// SteamNetConnectionInfo_t::m_nUserData, set on the connection right
// after it's created/accepted; each registers its own static handler as
// a per-connection/per-listen-socket config value (rather than one
// shared dispatcher) so a process hosting both a Client and a Server at
// once - as the standalone round-trip test does - never has to
// disambiguate whose connection it is.
namespace augusta::networking {

namespace {

// How many messages ReceiveMessages pulls out of GNS per underlying
// call - just a batch size for draining the queue in ReceiveMessages'
// own loop, not a cap on how many messages can be received overall.
constexpr int kMaxMessagesPerBatch = 16;

}  // namespace

void Init() {
  SteamNetworkingErrMsg err_msg;
  if (!GameNetworkingSockets_Init(nullptr, err_msg)) {
    throw std::runtime_error(std::string("networking::Init: ") + err_msg);
  }
}

void Shutdown() { GameNetworkingSockets_Kill(); }

// ---- Client ----

struct Client::Impl {
  std::mutex mutex;
  HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
  ConnectionState state = ConnectionState::kDisconnected;

  static void OnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    auto* self = reinterpret_cast<Impl*>(info->m_info.m_nUserData);
    if (self == nullptr) {
      return;
    }
    std::lock_guard<std::mutex> lock(self->mutex);
    switch (info->m_info.m_eState) {
      case k_ESteamNetworkingConnectionState_Connecting:
        self->state = ConnectionState::kConnecting;
        break;
      case k_ESteamNetworkingConnectionState_Connected:
        self->state = ConnectionState::kConnected;
        break;
      case k_ESteamNetworkingConnectionState_ClosedByPeer:
      case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SteamNetworkingSockets()->CloseConnection(info->m_hConn, 0, nullptr, false);
        self->connection = k_HSteamNetConnection_Invalid;
        self->state = ConnectionState::kDisconnected;
        break;
      default:
        break;
    }
  }
};

Client::Client() : impl_(std::make_unique<Impl>()) {}

Client::~Client() { Disconnect(); }

void Client::Connect(const Endpoint& server) {
  SteamNetworkingIPAddr addr;
  addr.Clear();
  if (!addr.ParseString(server.address.c_str())) {
    throw std::runtime_error("networking::Client::Connect: invalid address " + server.address);
  }

  // Both config values must be supplied here, not via a
  // SetConnectionUserData call after ConnectByIPAddress returns: GNS can
  // fire the first (Connecting) status-changed callback synchronously as
  // part of creating the connection, snapshotting whatever user data was
  // set at that point - which would be none, since Impl isn't reachable
  // from OnStatusChanged until this call supplies it up front.
  std::array<SteamNetworkingConfigValue_t, 2> options;
  options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                    reinterpret_cast<void*>(&Impl::OnStatusChanged));
  options[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, reinterpret_cast<intptr_t>(impl_.get()));

  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->state = ConnectionState::kConnecting;
  impl_->connection =
      SteamNetworkingSockets()->ConnectByIPAddress(addr, static_cast<int>(options.size()), options.data());
}

void Client::Disconnect() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->connection != k_HSteamNetConnection_Invalid) {
    SteamNetworkingSockets()->CloseConnection(impl_->connection, 0, nullptr, false);
    impl_->connection = k_HSteamNetConnection_Invalid;
  }
  impl_->state = ConnectionState::kDisconnected;
}

void Client::PumpEvents() { SteamNetworkingSockets()->RunCallbacks(); }

ConnectionState Client::GetState() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->state;
}

void Client::Send(const Payload& payload) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->state != ConnectionState::kConnected) {
    return;
  }
  SteamNetworkingSockets()->SendMessageToConnection(impl_->connection, payload.data(),
                                                    static_cast<std::uint32_t>(payload.size()),
                                                    k_nSteamNetworkingSend_Unreliable, nullptr);
}

std::vector<Payload> Client::ReceiveMessages() {
  HSteamNetConnection connection;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    connection = impl_->connection;
  }
  if (connection == k_HSteamNetConnection_Invalid) {
    return {};
  }

  std::vector<Payload> messages;
  std::array<ISteamNetworkingMessage*, kMaxMessagesPerBatch> incoming;
  int count = 0;
  while ((count = SteamNetworkingSockets()->ReceiveMessagesOnConnection(connection, incoming.data(),
                                                                        kMaxMessagesPerBatch)) > 0) {
    for (int i = 0; i < count; ++i) {
      const auto* bytes = static_cast<const std::byte*>(incoming[i]->m_pData);
      messages.emplace_back(bytes, bytes + incoming[i]->m_cbSize);
      incoming[i]->Release();
    }
  }
  return messages;
}

// ---- Server ----

struct Server::Impl {
  std::mutex mutex;
  HSteamListenSocket listen_socket = k_HSteamListenSocket_Invalid;
  HSteamNetPollGroup poll_group = k_HSteamNetPollGroup_Invalid;
  // Peers awaiting Accept/Disconnect - re-delivered as kConnectRequested
  // on every PumpEvents call until one of those resolves them (see
  // Server::PumpEvents's own doc comment in networking.h).
  std::unordered_set<HSteamNetConnection> pending_peers;
  // Peers currently past Accept and fully connected - Send/Broadcast's
  // only way to reach a peer, since GNS has no "send to poll group" call.
  std::unordered_set<HSteamNetConnection> connected_peers;
  // One-shot lifecycle transitions (kConnected/kDisconnected) queued by
  // OnStatusChanged since the last PumpEvents call.
  std::vector<PeerEvent> queued_events;

  static void OnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    auto* self = reinterpret_cast<Impl*>(info->m_info.m_nUserData);
    if (self == nullptr) {
      return;
    }
    const auto peer = static_cast<PeerId>(info->m_hConn);
    std::lock_guard<std::mutex> lock(self->mutex);
    switch (info->m_info.m_eState) {
      case k_ESteamNetworkingConnectionState_Connecting:
        self->pending_peers.insert(info->m_hConn);
        break;
      case k_ESteamNetworkingConnectionState_Connected:
        SteamNetworkingSockets()->SetConnectionPollGroup(info->m_hConn, self->poll_group);
        self->connected_peers.insert(info->m_hConn);
        self->queued_events.push_back(PeerEvent{.peer = peer, .type = PeerEventType::kConnected});
        break;
      case k_ESteamNetworkingConnectionState_ClosedByPeer:
      case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SteamNetworkingSockets()->CloseConnection(info->m_hConn, 0, nullptr, false);
        self->pending_peers.erase(info->m_hConn);
        self->connected_peers.erase(info->m_hConn);
        self->queued_events.push_back(PeerEvent{.peer = peer, .type = PeerEventType::kDisconnected});
        break;
      default:
        break;
    }
  }
};

Server::Server(const Endpoint& local_endpoint) : impl_(std::make_unique<Impl>()) {
  SteamNetworkingIPAddr addr;
  addr.Clear();
  if (!addr.ParseString(local_endpoint.address.c_str())) {
    throw std::runtime_error("networking::Server: invalid address " + local_endpoint.address);
  }

  // Two config values applied to every connection accepted through this
  // listen socket: the status-changed callback, and a default user data
  // value carrying impl_.get() - set here, before any connection exists,
  // so OnStatusChanged can already resolve it on that connection's very
  // first (Connecting) callback. Client::Connect instead sets user data
  // itself right after ConnectByIPAddress returns, since it has only one
  // connection to tag and no listen socket to inherit a default from.
  std::array<SteamNetworkingConfigValue_t, 2> options;
  options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                    reinterpret_cast<void*>(&Impl::OnStatusChanged));
  options[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, reinterpret_cast<intptr_t>(impl_.get()));

  impl_->poll_group = SteamNetworkingSockets()->CreatePollGroup();
  impl_->listen_socket =
      SteamNetworkingSockets()->CreateListenSocketIP(addr, static_cast<int>(options.size()), options.data());
  if (impl_->listen_socket == k_HSteamListenSocket_Invalid) {
    throw std::runtime_error("networking::Server: failed to bind " + local_endpoint.address);
  }
}

Server::~Server() {
  for (HSteamNetConnection connection : impl_->pending_peers) {
    SteamNetworkingSockets()->CloseConnection(connection, 0, nullptr, false);
  }
  for (HSteamNetConnection connection : impl_->connected_peers) {
    SteamNetworkingSockets()->CloseConnection(connection, 0, nullptr, false);
  }
  SteamNetworkingSockets()->CloseListenSocket(impl_->listen_socket);
  SteamNetworkingSockets()->DestroyPollGroup(impl_->poll_group);
}

std::vector<PeerEvent> Server::PumpEvents() {
  SteamNetworkingSockets()->RunCallbacks();

  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<PeerEvent> events = std::move(impl_->queued_events);
  impl_->queued_events.clear();
  for (HSteamNetConnection peer : impl_->pending_peers) {
    events.push_back(PeerEvent{.peer = static_cast<PeerId>(peer), .type = PeerEventType::kConnectRequested});
  }
  return events;
}

void Server::Accept(PeerId peer) {
  const auto connection = static_cast<HSteamNetConnection>(peer);
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pending_peers.erase(connection);
  }
  SteamNetworkingSockets()->AcceptConnection(connection);
}

void Server::Disconnect(PeerId peer) {
  const auto connection = static_cast<HSteamNetConnection>(peer);
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pending_peers.erase(connection);
    impl_->connected_peers.erase(connection);
  }
  SteamNetworkingSockets()->CloseConnection(connection, 0, nullptr, false);
}

void Server::Send(PeerId peer, const Payload& payload) {
  const auto connection = static_cast<HSteamNetConnection>(peer);
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->connected_peers.contains(connection)) {
      return;
    }
  }
  SteamNetworkingSockets()->SendMessageToConnection(connection, payload.data(),
                                                    static_cast<std::uint32_t>(payload.size()),
                                                    k_nSteamNetworkingSend_Unreliable, nullptr);
}

void Server::Broadcast(const Payload& payload) {
  std::vector<HSteamNetConnection> peers;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    peers.assign(impl_->connected_peers.begin(), impl_->connected_peers.end());
  }
  for (HSteamNetConnection connection : peers) {
    SteamNetworkingSockets()->SendMessageToConnection(connection, payload.data(),
                                                      static_cast<std::uint32_t>(payload.size()),
                                                      k_nSteamNetworkingSend_Unreliable, nullptr);
  }
}

std::vector<PeerMessage> Server::ReceiveMessages() {
  std::vector<PeerMessage> messages;
  std::array<ISteamNetworkingMessage*, kMaxMessagesPerBatch> incoming;
  int count = 0;
  while ((count = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(impl_->poll_group, incoming.data(),
                                                                       kMaxMessagesPerBatch)) > 0) {
    for (int i = 0; i < count; ++i) {
      const auto* bytes = static_cast<const std::byte*>(incoming[i]->m_pData);
      messages.push_back(PeerMessage{.from = static_cast<PeerId>(incoming[i]->m_conn),
                                     .payload = Payload(bytes, bytes + incoming[i]->m_cbSize)});
      incoming[i]->Release();
    }
  }
  return messages;
}

}  // namespace augusta::networking
