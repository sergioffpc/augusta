#include "augusta/networking.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>  // SteamNetworkingIPAddr::ParseString's inline body lives here.
#include <steam/steamclientpublic.h>
#include <steam/steamnetworkingsockets.h>
#include <steam/steamnetworkingtypes.h>

#include "augusta/logging.h"
#include "send_flags.h"
#include "transport_events.h"

// M1 spike (ADR-0003): the first real (non-stub) body for this module.
// Both Client and Server route GameNetworkingSockets' single global
// connection-status-changed callback back to their own Impl via
// SteamNetConnectionInfo_t::m_nUserData, set on the connection right
// after it's created/accepted; each registers its own static handler as
// a per-connection/per-listen-socket config value (rather than one
// shared dispatcher) so a process hosting both a Client and a Server at
// once - as the standalone round-trip test does - never has to
// disambiguate whose connection it is. The handler only publishes the
// event (transport_events.h); PumpEvents, the Network I/O owner, applies it.
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
class StatusHandlerRegistry {
 public:
  std::int64_t Add(StatusHandler handler) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::int64_t handler_id = next_id_++;
    handlers_.emplace(handler_id, std::move(handler));
    return handler_id;
  }

  void Remove(std::int64_t handler_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    handlers_.erase(handler_id);
  }

  void Dispatch(std::int64_t handler_id, SteamNetConnectionStatusChangedCallback_t* info) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (const auto found = handlers_.find(handler_id); found != handlers_.end()) {
      found->second(info);
    }
  }

 private:
  std::mutex mutex_;
  // 0 is GameNetworkingSockets' "no user data".
  std::int64_t next_id_ = 1;
  std::unordered_map<std::int64_t, StatusHandler> handlers_;
};

StatusHandlerRegistry& StatusHandlers() {
  static StatusHandlerRegistry registry;
  return registry;
}

// Registers a handler for as long as it lives. An owner declares it as its
// last member, so it unregisters first - before the members the handler
// reads are destroyed.
class StatusHandlerRegistration {
 public:
  explicit StatusHandlerRegistration(StatusHandler handler) : id_(StatusHandlers().Add(std::move(handler))) {}
  ~StatusHandlerRegistration() { StatusHandlers().Remove(id_); }
  StatusHandlerRegistration(const StatusHandlerRegistration&) = delete;
  StatusHandlerRegistration& operator=(const StatusHandlerRegistration&) = delete;

  // What to set as the connection's user data so its events reach the handler.
  [[nodiscard]] std::int64_t Id() const { return id_; }

 private:
  std::int64_t id_;
};

// The status-changed callback both roles register with GameNetworkingSockets.
void OnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
  StatusHandlers().Dispatch(info->m_info.m_nUserData, info);
}

TransportState ToTransportState(ESteamNetworkingConnectionState state) {
  switch (state) {
    case k_ESteamNetworkingConnectionState_Connecting:
      return TransportState::kConnecting;
    case k_ESteamNetworkingConnectionState_Connected:
      return TransportState::kConnected;
    case k_ESteamNetworkingConnectionState_ClosedByPeer:
      return TransportState::kClosedByPeer;
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
      return TransportState::kProblemDetectedLocally;
    default:
      return TransportState::kOther;
  }
}

// What a status-changed callback publishes: the minimal event, and nothing else.
TransportEvent ToTransportEvent(const SteamNetConnectionStatusChangedCallback_t& info) {
  return TransportEvent{.connection = info.m_hConn,
                        .state = ToTransportState(info.m_info.m_eState),
                        .remote_address = FormatAddr(info.m_info.m_addrRemote)};
}

// The one log line for a connection's state change, written by the Network I/O
// owner as it applies the event, not by the callback.
void LogTransportEvent(std::string_view role, const TransportEvent& event) {
  switch (event.state) {
    case TransportState::kConnecting:
      LD("subsystem=networking event=state_changed role={} state=connecting peer={} peer_addr={}", role,
         event.connection, event.remote_address);
      break;
    case TransportState::kConnected:
      LI("subsystem=networking event=state_changed role={} state=connected peer={} peer_addr={}", role,
         event.connection, event.remote_address);
      break;
    case TransportState::kClosedByPeer:
      LI("subsystem=networking event=state_changed role={} state=disconnected peer={} peer_addr={} "
         "reason=closed_by_peer",
         role, event.connection, event.remote_address);
      break;
    case TransportState::kProblemDetectedLocally:
      LW("subsystem=networking event=state_changed role={} state=disconnected peer={} peer_addr={} "
         "reason=problem_detected_locally",
         role, event.connection, event.remote_address);
      break;
    case TransportState::kOther:
      break;
  }
}

// Makes call for connection: the transport work an event asked for, run by the
// Network I/O owner with no lock held.
void MakeTransportCall(TransportCall call, HSteamNetConnection connection, HSteamNetPollGroup poll_group) {
  switch (call) {
    case TransportCall::kNone:
      break;
    case TransportCall::kJoinPollGroup:
      SteamNetworkingSockets()->SetConnectionPollGroup(connection, poll_group);
      break;
    case TransportCall::kClose:
      SteamNetworkingSockets()->CloseConnection(connection, 0, nullptr, false);
      break;
  }
}

// Listens on addr with options. GameNetworkingSockets refuses port 0, so for
// it this picks ports of the dynamic range (IANA's 49152-65535) at random
// until one binds - a port another socket holds fails to bind, and is passed
// over - and leaves the one it bound in addr.
HSteamListenSocket CreateListenSocket(SteamNetworkingIPAddr& addr,
                                      std::span<const SteamNetworkingConfigValue_t> options) {
  const auto create = [&] {
    return SteamNetworkingSockets()->CreateListenSocketIP(addr, static_cast<int>(options.size()), options.data());
  };
  if (addr.m_port != 0) {
    return create();
  }
  // Enough that only a range nearly all taken runs out: with a handful of
  // servers on the machine, a single attempt already all but always binds.
  constexpr int kAttempts = 32;
  constexpr std::uint16_t kFirstDynamicPort = 49152;
  std::random_device seed;
  std::mt19937 engine(seed());
  std::uniform_int_distribution<std::uint16_t> ports(kFirstDynamicPort, std::numeric_limits<std::uint16_t>::max());
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    addr.m_port = ports(engine);
    if (const HSteamListenSocket socket = create(); socket != k_HSteamListenSocket_Invalid) {
      return socket;
    }
  }
  return k_HSteamListenSocket_Invalid;
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
  // Every packet draws a jitter; a mean of 0 draws none.
  constexpr float kEveryPacket = 100.0F;
  utils->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Avg,
                                   static_cast<float>(conditions.jitter_mean_ms));
  utils->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Max,
                                   static_cast<float>(conditions.jitter_max_ms));
  utils->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Pct, kEveryPacket);
  utils->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, conditions.loss_percent);
  utils->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketReorder_Send, conditions.reorder_percent);
  utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketReorder_Time, conditions.reorder_delay_ms);
  for (const ESteamNetworkingConfigValue timeout :
       {k_ESteamNetworkingConfig_TimeoutInitial, k_ESteamNetworkingConfig_TimeoutConnected}) {
    if (conditions.timeout_ms > 0) {
      utils->SetGlobalConfigValueInt32(timeout, conditions.timeout_ms);
    } else {
      // A null value clears the override, back to the library's default.
      utils->SetConfigValue(timeout, k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Int32, nullptr);
    }
  }
  LI("subsystem=networking event=simulated_conditions latency_ms={} jitter_mean_ms={} jitter_max_ms={} "
     "loss_percent={} reorder_percent={} reorder_delay_ms={}",
     conditions.latency_ms, conditions.jitter_mean_ms, conditions.jitter_max_ms, conditions.loss_percent,
     conditions.reorder_percent, conditions.reorder_delay_ms);
}

// ---- Client ----

struct Client::Impl {
  std::mutex mutex;
  HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
  ConnectionState state = ConnectionState::kDisconnected;
  // Filled by the status-changed callback, drained by PumpEvents.
  TransportEventQueue transport_events;

  // Applies one drained event: the state under the lock, then the transport
  // call it needs and its log line outside it.
  void Apply(const TransportEvent& event) {
    ClientTransition transition;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      transition = ApplyToClient(connection, state, event);
      state = transition.state;
      if (transition.ended) {
        connection = k_HSteamNetConnection_Invalid;
      }
    }
    MakeTransportCall(transition.call, event.connection, k_HSteamNetPollGroup_Invalid);
    LogTransportEvent("client", event);
  }

  // Last, so it unregisters before the queue the callback publishes to goes.
  StatusHandlerRegistration registration{
      [this](SteamNetConnectionStatusChangedCallback_t* info) { transport_events.Publish(ToTransportEvent(*info)); }};
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
  // SetConnectionUserData call after ConnectByIPAddress returns: GNS queues
  // the first (Connecting) status-changed callback while creating the
  // connection, snapshotting the user data in effect at that point - which
  // would be none, since Impl isn't reachable from OnStatusChanged until
  // this call supplies it up front.
  //
  // Holding the mutex across ConnectByIPAddress cannot deadlock: GNS only
  // invokes queued callbacks from RunCallbacks (PumpEvents), never from
  // inside the call that queued them.
  std::array<SteamNetworkingConfigValue_t, 2> options;
  options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                    reinterpret_cast<void*>(&OnStatusChanged));
  options[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, impl_->registration.Id());

  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->connection =
      SteamNetworkingSockets()->ConnectByIPAddress(addr, static_cast<int>(options.size()), options.data());
  // A connection GNS refused to create never gets a status-changed callback,
  // so this is the only place that can report it.
  if (impl_->connection == k_HSteamNetConnection_Invalid) {
    impl_->state = ConnectionState::kDisconnected;
    LW("subsystem=networking event=state_changed role=client state=disconnected reason=connection_not_created "
       "server_addr={}",
       server.address);
    return;
  }
  impl_->state = ConnectionState::kConnecting;
}

void Client::Disconnect() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->connection != k_HSteamNetConnection_Invalid) {
    SteamNetworkingSockets()->CloseConnection(impl_->connection, 0, nullptr, false);
    impl_->connection = k_HSteamNetConnection_Invalid;
  }
  impl_->state = ConnectionState::kDisconnected;
}

void Client::PumpEvents() {
  SteamNetworkingSockets()->RunCallbacks();
  for (const TransportEvent& event : impl_->transport_events.Drain()) {
    impl_->Apply(event);
  }
}

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
  // What LocalEndpoint reports, set once the listen socket is bound.
  Endpoint local_endpoint;
  // Pending peers, re-delivered as kConnectRequested on every PumpEvents call
  // until Accept/Disconnect answers them (see Server::PumpEvents's own doc
  // comment in networking.h); connected peers, Send/Broadcast's only way to
  // reach a peer, since GNS has no "send to poll group" call; and the one-shot
  // events since the last PumpEvents. Guarded by mutex.
  PeerTable peers;
  // Filled by the status-changed callback, drained by PumpEvents.
  TransportEventQueue transport_events;

  // Applies one drained event: the peers under the lock, then the transport
  // call it needs and its log line outside it.
  void Apply(const TransportEvent& event) {
    TransportCall call = TransportCall::kNone;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      call = peers.Apply(event);
    }
    MakeTransportCall(call, event.connection, poll_group);
    LogTransportEvent("server", event);
  }

  // Last, so it unregisters before the queue the callback publishes to goes.
  StatusHandlerRegistration registration{
      [this](SteamNetConnectionStatusChangedCallback_t* info) { transport_events.Publish(ToTransportEvent(*info)); }};
};

Server::Server(const Endpoint& local_endpoint) : impl_(std::make_unique<Impl>()) {
  SteamNetworkingIPAddr addr;
  addr.Clear();
  if (!addr.ParseString(local_endpoint.address.c_str())) {
    throw std::runtime_error("networking::Server: invalid address " + local_endpoint.address);
  }

  // Two config values applied to every connection accepted through this
  // listen socket: the status-changed callback, and a default user data
  // value carrying this server's handler registration - set here, before
  // any connection exists, so OnStatusChanged can already resolve it on
  // that connection's very first (Connecting) callback. Client::Connect
  // passes the same two values to ConnectByIPAddress instead, since it has
  // only one connection to tag and no listen socket to inherit them from.
  std::array<SteamNetworkingConfigValue_t, 2> options;
  options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                    reinterpret_cast<void*>(&OnStatusChanged));
  options[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, impl_->registration.Id());

  impl_->poll_group = SteamNetworkingSockets()->CreatePollGroup();
  impl_->listen_socket = CreateListenSocket(addr, options);
  if (impl_->listen_socket == k_HSteamListenSocket_Invalid) {
    throw std::runtime_error("networking::Server: failed to bind " + local_endpoint.address);
  }
  impl_->local_endpoint = Endpoint{.address = FormatAddr(addr)};
  LI("subsystem=networking event=listening address={}", impl_->local_endpoint.address);
}

Endpoint Server::LocalEndpoint() const { return impl_->local_endpoint; }

Server::~Server() {
  for (const HSteamNetConnection connection : impl_->peers.Connections()) {
    SteamNetworkingSockets()->CloseConnection(connection, 0, nullptr, false);
  }
  SteamNetworkingSockets()->CloseListenSocket(impl_->listen_socket);
  SteamNetworkingSockets()->DestroyPollGroup(impl_->poll_group);
}

std::vector<PeerEvent> Server::PumpEvents() {
  SteamNetworkingSockets()->RunCallbacks();
  for (const TransportEvent& event : impl_->transport_events.Drain()) {
    impl_->Apply(event);
  }

  const std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->peers.TakeEvents();
}

void Server::Accept(PeerId peer) {
  const auto connection = static_cast<HSteamNetConnection>(peer);
  {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->peers.Answer(connection);
  }
  SteamNetworkingSockets()->AcceptConnection(connection);
}

void Server::Disconnect(PeerId peer) {
  const auto connection = static_cast<HSteamNetConnection>(peer);
  {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->peers.Forget(connection);
  }
  // Lingering, so what was already sent reliably - a reply that explains the
  // disconnect, such as a Join refusal - still arrives before the connection ends.
  SteamNetworkingSockets()->CloseConnection(connection, 0, nullptr, true);
}

void Server::Send(PeerId peer, const Payload& payload, Reliability reliability) {
  const auto connection = static_cast<HSteamNetConnection>(peer);
  {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->peers.IsConnected(connection)) {
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
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    peers = impl_->peers.Connected();
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
