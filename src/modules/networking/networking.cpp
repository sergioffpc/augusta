#include "augusta/networking.h"

#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>  // SteamNetworkingIPAddr::ParseString's inline body lives here.
#include <steam/steamnetworkingsockets.h>

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "augusta/logging.h"

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

// Peer IPs are fine to log (ADR-0029) - direct-IP connections, no
// matchmaking/relay to anonymize, so the server already sees them.
std::string FormatAddr(const SteamNetworkingIPAddr& addr) {
  std::array<char, SteamNetworkingIPAddr::k_cchMaxString> buf;
  addr.ToString(buf.data(), buf.size(), /*bWithPort=*/true);
  return buf.data();
}

using StatusHandler = std::function<void(SteamNetConnectionStatusChangedCallback_t*)>;

// GameNetworkingSockets queues connection-status events and delivers them on
// the next RunCallbacks, each carrying the user data it was created with - so
// an event for a connection whose Client/Server has since been destroyed
// still arrives. The user data is therefore an id looked up here, never a
// pointer: a destroyed owner's id is gone from the map and its events are
// dropped, and ids are never reused, so a new owner allocated at the same
// address cannot receive them. The lock is held while a handler runs, so an
// owner cannot finish unregistering (and be destroyed) mid-callback.
class LiveHandlers {
 public:
  std::int64_t Add(StatusHandler handler) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::int64_t id = next_id_++;
    handlers_.emplace(id, std::move(handler));
    return id;
  }

  void Remove(std::int64_t id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    handlers_.erase(id);
  }

  void Dispatch(std::int64_t id, SteamNetConnectionStatusChangedCallback_t* info) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (const auto found = handlers_.find(id); found != handlers_.end()) {
      found->second(info);
    }
  }

 private:
  std::mutex mutex_;
  // 0 is GameNetworkingSockets' "no user data".
  std::int64_t next_id_ = 1;
  std::unordered_map<std::int64_t, StatusHandler> handlers_;
};

LiveHandlers& LiveNetworkingHandlers() {
  static LiveHandlers live;
  return live;
}

// Registers a handler for as long as it lives. An owner declares it as its
// last member, so it unregisters first - before the members the handler
// reads are destroyed.
class LiveRegistration {
 public:
  explicit LiveRegistration(StatusHandler handler) : id_(LiveNetworkingHandlers().Add(std::move(handler))) {}
  ~LiveRegistration() { LiveNetworkingHandlers().Remove(id_); }
  LiveRegistration(const LiveRegistration&) = delete;
  LiveRegistration& operator=(const LiveRegistration&) = delete;

  // What to set as the connection's user data so its events reach the handler.
  [[nodiscard]] std::int64_t id() const { return id_; }

 private:
  std::int64_t id_;
};

// The status-changed callback both roles register with GameNetworkingSockets.
void OnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
  LiveNetworkingHandlers().Dispatch(info->m_info.m_nUserData, info);
}

int SendFlags(Reliability reliability) {
  return reliability == Reliability::kReliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;
}

}  // namespace

void Init() {
  SteamNetworkingErrMsg err_msg;
  if (!GameNetworkingSockets_Init(nullptr, err_msg)) {
    LE("subsystem=networking event=init_failed reason={}", err_msg);
    throw std::runtime_error(std::string("networking::Init: ") + err_msg);
  }
  LI("subsystem=networking event=init");
}

void Shutdown() {
  GameNetworkingSockets_Kill();
  LI("subsystem=networking event=shutdown");
}

void SimulateNetworkConditions(const SimulatedConditions& conditions) {
  ISteamNetworkingUtils* utils = SteamNetworkingUtils();
  utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send, conditions.latency_ms);
  utils->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, conditions.loss_percent);
  LI("subsystem=networking event=simulated_conditions latency_ms={} loss_percent={}", conditions.latency_ms,
     conditions.loss_percent);
}

// ---- Client ----

struct Client::Impl {
  std::mutex mutex;
  HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
  ConnectionState state = ConnectionState::kDisconnected;

  void HandleStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    std::lock_guard<std::mutex> lock(mutex);
    switch (info->m_info.m_eState) {
      case k_ESteamNetworkingConnectionState_Connecting:
        state = ConnectionState::kConnecting;
        LD("subsystem=networking event=state_changed role=client state=connecting");
        break;
      case k_ESteamNetworkingConnectionState_Connected:
        state = ConnectionState::kConnected;
        LI("subsystem=networking event=state_changed role=client state=connected");
        break;
      case k_ESteamNetworkingConnectionState_ClosedByPeer:
      case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SteamNetworkingSockets()->CloseConnection(info->m_hConn, 0, nullptr, false);
        connection = k_HSteamNetConnection_Invalid;
        state = ConnectionState::kDisconnected;
        if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
          LW("subsystem=networking event=state_changed role=client state=disconnected "
             "reason=problem_detected_locally");
        } else {
          LI("subsystem=networking event=state_changed role=client state=disconnected reason=closed_by_peer");
        }
        break;
      default:
        break;
    }
  }

  // Last, so it unregisters before the members HandleStatusChanged reads go.
  LiveRegistration registration{[this](SteamNetConnectionStatusChangedCallback_t* info) { HandleStatusChanged(info); }};
};

Client::Client() : impl_(std::make_unique<Impl>()) {}

Client::~Client() { Disconnect(); }

void Client::Connect(const Endpoint& server) {
  SteamNetworkingIPAddr addr;
  addr.Clear();
  if (!addr.ParseString(server.address.c_str())) {
    throw std::runtime_error("networking::Client::Connect: invalid address " + server.address);
  }
  LI("subsystem=networking event=connecting role=client server_addr={}", server.address);

  // Both config values must be supplied here, not via a
  // SetConnectionUserData call after ConnectByIPAddress returns: GNS can
  // fire the first (Connecting) status-changed callback synchronously as
  // part of creating the connection, snapshotting whatever user data was
  // set at that point - which would be none, since Impl isn't reachable
  // from OnStatusChanged until this call supplies it up front.
  std::array<SteamNetworkingConfigValue_t, 2> options;
  options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                    reinterpret_cast<void*>(&OnStatusChanged));
  options[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, impl_->registration.id());

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

std::optional<ConnectionStats> Client::GetStats() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->state != ConnectionState::kConnected) {
    return std::nullopt;
  }

  SteamNetConnectionRealTimeStatus_t status;
  if (SteamNetworkingSockets()->GetConnectionRealTimeStatus(impl_->connection, &status, 0, nullptr) != k_EResultOK) {
    return std::nullopt;
  }

  return ConnectionStats{
      .ping_ms = status.m_nPing,
      .quality_local = status.m_flConnectionQualityLocal,
      .quality_remote = status.m_flConnectionQualityRemote,
      .in_bytes_per_sec = status.m_flInBytesPerSec,
      .out_bytes_per_sec = status.m_flOutBytesPerSec,
      .max_jitter_us = status.m_usecMaxJitter,
      .pending_bytes = status.m_cbPendingUnreliable + status.m_cbPendingReliable + status.m_cbSentUnackedReliable,
  };
}

void Client::Send(const Payload& payload, Reliability reliability) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->state != ConnectionState::kConnected) {
    return;
  }
  LT("subsystem=networking event=send role=client bytes={}", payload.size());
  SteamNetworkingSockets()->SendMessageToConnection(
      impl_->connection, payload.data(), static_cast<std::uint32_t>(payload.size()), SendFlags(reliability), nullptr);
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
  if (!messages.empty()) {
    LT("subsystem=networking event=receive role=client count={}", messages.size());
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

  void HandleStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    const auto peer = static_cast<PeerId>(info->m_hConn);
    const auto peer_id = static_cast<std::uint32_t>(peer);
    const std::string peer_addr = FormatAddr(info->m_info.m_addrRemote);
    std::lock_guard<std::mutex> lock(mutex);
    switch (info->m_info.m_eState) {
      case k_ESteamNetworkingConnectionState_Connecting:
        pending_peers.insert(info->m_hConn);
        LD("subsystem=networking event=state_changed role=server state=connecting peer={} peer_addr={}", peer_id,
           peer_addr);
        break;
      case k_ESteamNetworkingConnectionState_Connected:
        SteamNetworkingSockets()->SetConnectionPollGroup(info->m_hConn, poll_group);
        connected_peers.insert(info->m_hConn);
        queued_events.push_back(PeerEvent{.peer = peer, .type = PeerEventType::kConnected});
        LI("subsystem=networking event=state_changed role=server state=connected peer={} peer_addr={}", peer_id,
           peer_addr);
        break;
      case k_ESteamNetworkingConnectionState_ClosedByPeer:
      case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SteamNetworkingSockets()->CloseConnection(info->m_hConn, 0, nullptr, false);
        pending_peers.erase(info->m_hConn);
        connected_peers.erase(info->m_hConn);
        queued_events.push_back(PeerEvent{.peer = peer, .type = PeerEventType::kDisconnected});
        if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
          LW("subsystem=networking event=state_changed role=server state=disconnected peer={} peer_addr={} "
             "reason=problem_detected_locally",
             peer_id, peer_addr);
        } else {
          LI("subsystem=networking event=state_changed role=server state=disconnected peer={} peer_addr={} "
             "reason=closed_by_peer",
             peer_id, peer_addr);
        }
        break;
      default:
        break;
    }
  }

  // Last, so it unregisters before the members HandleStatusChanged reads go.
  LiveRegistration registration{[this](SteamNetConnectionStatusChangedCallback_t* info) { HandleStatusChanged(info); }};
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
                    reinterpret_cast<void*>(&OnStatusChanged));
  options[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, impl_->registration.id());

  impl_->poll_group = SteamNetworkingSockets()->CreatePollGroup();
  impl_->listen_socket =
      SteamNetworkingSockets()->CreateListenSocketIP(addr, static_cast<int>(options.size()), options.data());
  if (impl_->listen_socket == k_HSteamListenSocket_Invalid) {
    throw std::runtime_error("networking::Server: failed to bind " + local_endpoint.address);
  }
  LI("subsystem=networking event=listening address={}", local_endpoint.address);
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

void Server::Send(PeerId peer, const Payload& payload, Reliability reliability) {
  const auto connection = static_cast<HSteamNetConnection>(peer);
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->connected_peers.contains(connection)) {
      return;
    }
  }
  LT("subsystem=networking event=send role=server peer={} bytes={}", static_cast<std::uint32_t>(peer), payload.size());
  SteamNetworkingSockets()->SendMessageToConnection(
      connection, payload.data(), static_cast<std::uint32_t>(payload.size()), SendFlags(reliability), nullptr);
}

void Server::Broadcast(const Payload& payload, Reliability reliability) {
  std::vector<HSteamNetConnection> peers;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    peers.assign(impl_->connected_peers.begin(), impl_->connected_peers.end());
  }
  LT("subsystem=networking event=broadcast role=server peers={} bytes={}", peers.size(), payload.size());
  for (HSteamNetConnection connection : peers) {
    SteamNetworkingSockets()->SendMessageToConnection(
        connection, payload.data(), static_cast<std::uint32_t>(payload.size()), SendFlags(reliability), nullptr);
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
  if (!messages.empty()) {
    LT("subsystem=networking event=receive role=server count={}", messages.size());
  }
  return messages;
}

}  // namespace augusta::networking
