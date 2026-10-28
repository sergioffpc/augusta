#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>  // SteamNetworkingIPAddr::ParseString's inline body lives here.
#include <steam/steamnetworkingtypes.h>

#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "gns_adapter.h"
#include "send_flags.h"
#include "status_handlers.h"
#include "tap_endpoints.h"
#include "transport_events.h"

namespace augusta::networking {

struct Server::Impl {
  explicit Impl(failure::Faults* faults) : faults(faults) {}

  // Null outside tests.
  failure::Faults* const faults;
  // Guards peers, and is held across every send, so no send can race the
  // close of its connection: each close forgets the connection under it
  // first. A connection handle the transport calls invalid is then its own
  // failure, never a peer that just left.
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
#if AUGUSTA_DEBUG_TOOLS
  // Null unless a Tap was given; local is the listen address, set once bound.
  Tap* tap = nullptr;
  TapEndpoint local;
#endif

  // Applies one drained event: the peers under the lock, then the transport
  // call it needs and its log line outside it.
  void Apply(const TransportEvent& event) {
    TransportCall call = TransportCall::kNone;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      call = peers.Apply(event);
    }
    const bool joined = MakeTransportCall(call, event.connection, poll_group);
    LogTransportEvent("server", event);
    if (!joined) {
      // The poll group is valid from construction on, so it is the connection
      // that is gone: the peer's outcome, reported as its departure.
      LOG_WARN(
          "subsystem=networking event=state_changed role=server state=disconnected peer={} "
          "reason=poll_group_join_failed",
          event.connection);
      {
        const std::lock_guard<std::mutex> lock(mutex);
        call = peers.Lose(event.connection);
      }
      MakeTransportCall(call, event.connection, poll_group);
    }
  }

  // Sends under mutex, which the caller holds: kDropped if connection is not
  // a connected peer. Sets call to the transport call left to make once mutex
  // is released (kClose, for a connection it ended).
  SendResult SendLocked(HSteamNetConnection connection, const Payload& payload, Reliability reliability,
                        TransportCall& call);

  // Last, so it unregisters before the queue the callback publishes to goes.
  StatusHandlerRegistration registration{
      [this](SteamNetConnectionStatusChangedCallback_t* info) { transport_events.Publish(ToTransportEvent(*info)); }};
};

Server::Server(const Endpoint& local_endpoint, failure::Faults* faults, std::size_t max_receive_size)
    : impl_(std::make_unique<Impl>(faults)) {
  SteamNetworkingIPAddr addr;
  addr.Clear();
  if (!addr.ParseString(local_endpoint.address.c_str())) {
    throw UnparsedAddress(local_endpoint.address);
  }

  // Three config values applied to every connection accepted through this
  // listen socket. First the status-changed callback, and a default user
  // data value carrying this server's handler registration - set here,
  // before any connection exists, so OnStatusChanged can already resolve it
  // on that connection's very first (Connecting) callback. Client::Connect
  // passes these two to ConnectByIPAddress instead, since it has only one
  // connection to tag and no listen socket to inherit them from. Last, the
  // largest message a peer may send, past which the transport closes its
  // connection.
  std::array<SteamNetworkingConfigValue_t, 3> options;
  options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                    reinterpret_cast<void*>(&OnStatusChanged));
  options[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, impl_->registration.Id());
  options[2].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize,
                      static_cast<std::int32_t>(std::min<std::size_t>(max_receive_size, kDefaultMaxReceiveSize)));

  const auto listener_failure = [&](std::string detail) {
    return failure::ClassifiedFailure(LocalFailure(
        failure::Code::kListenerSetupFailed, {{.key = "address", .value = local_endpoint.address}}, std::move(detail)));
  };
  if (faults != nullptr) {
    if (std::optional<std::string> tripped = faults->Trip(failure::Site::kListenerSetup)) {
      throw listener_failure(std::move(*tripped));
    }
  }
  impl_->poll_group = SteamNetworkingSockets()->CreatePollGroup();
  if (impl_->poll_group == k_HSteamNetPollGroup_Invalid) {
    throw listener_failure("CreatePollGroup failed");
  }
  impl_->listen_socket = CreateListenSocket(addr, options);
  if (impl_->listen_socket == k_HSteamListenSocket_Invalid) {
    // The destructor does not run for a constructor that throws.
    SteamNetworkingSockets()->DestroyPollGroup(impl_->poll_group);
    throw listener_failure("failed to bind");
  }
  impl_->local_endpoint = Endpoint{.address = FormatAddr(addr)};
#if AUGUSTA_DEBUG_TOOLS
  impl_->local = ToTapEndpoint(addr);
#endif
  LOG_INFO("subsystem=networking event=listening address={}", impl_->local_endpoint.address);
}

#if AUGUSTA_DEBUG_TOOLS
Server::Server(const Endpoint& local_endpoint, failure::Faults* faults, Tap* tap, std::size_t max_receive_size)
    : Server(local_endpoint, faults, max_receive_size) {
  impl_->tap = tap;
}
#endif

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

SendResult Server::Impl::SendLocked(HSteamNetConnection connection, const Payload& payload, Reliability reliability,
                                    TransportCall& call) {
  if (!peers.IsConnected(connection)) {
    return SendOutcome::kDropped;
  }
  const Sent sent = SendToConnection(faults, connection, payload, reliability);
  switch (ClassifySend(sent.result, reliability)) {
    case SendVerdict::kAccepted:
      LOG_TRACE("subsystem=networking event=send role=server peer={} bytes={}", connection, payload.size());
#if AUGUSTA_DEBUG_TOOLS
      if (tap != nullptr) {
        ReportToTap(tap, TapDirection::kOutbound, local, RemoteTapEndpoint(connection), payload);
      }
#endif
      return SendOutcome::kAccepted;
    case SendVerdict::kDropped:
      return SendOutcome::kDropped;
    case SendVerdict::kDroppedEndingConnection:
      LOG_WARN("subsystem=networking event=state_changed role=server state=disconnected peer={} reason=send_queue_full",
               connection);
      call = peers.Lose(connection);
      return SendOutcome::kDropped;
    case SendVerdict::kLocalFailure:
      break;
  }
  return std::unexpected(LocalFailure(
      failure::Code::kTransportSendFailed,
      {{.key = "role", .value = "server"}, {.key = "peer", .value = std::to_string(connection)}}, sent.Detail()));
}

SendResult Server::Send(PeerId peer, const Payload& payload, Reliability reliability) {
  const auto connection = static_cast<HSteamNetConnection>(peer);
  TransportCall call = TransportCall::kNone;
  SendResult sent;
  {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    sent = impl_->SendLocked(connection, payload, reliability, call);
  }
  MakeTransportCall(call, connection, impl_->poll_group);
  return sent;
}

std::expected<void, failure::Failure> Server::Broadcast(const Payload& payload, Reliability reliability) {
  std::vector<HSteamNetConnection> lost;
  std::expected<void, failure::Failure> broadcast;
  {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    const std::vector<HSteamNetConnection> peers = impl_->peers.Connected();
    LOG_TRACE("subsystem=networking event=broadcast role=server peers={} bytes={}", peers.size(), payload.size());
    for (const HSteamNetConnection connection : peers) {
      TransportCall call = TransportCall::kNone;
      const SendResult sent = impl_->SendLocked(connection, payload, reliability, call);
      if (call == TransportCall::kClose) {
        lost.push_back(connection);
      }
      if (!sent.has_value()) {
        broadcast = std::unexpected(sent.error());
        break;
      }
    }
  }
  for (const HSteamNetConnection connection : lost) {
    MakeTransportCall(TransportCall::kClose, connection, impl_->poll_group);
  }
  return broadcast;
}

std::expected<std::vector<PeerMessage>, failure::Failure> Server::ReceiveMessages() {
  std::vector<PeerMessage> messages;
  std::array<ISteamNetworkingMessage*, kMaxMessagesPerBatch> incoming;
  while (true) {
    std::optional<std::string> tripped = ReceiveTripped(impl_->faults);
    const int count = tripped.has_value() ? -1
                                          : SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(
                                                impl_->poll_group, incoming.data(), kMaxMessagesPerBatch);
    if (count < 0) {
      return std::unexpected(LocalFailure(failure::Code::kTransportReceiveFailed, {{.key = "role", .value = "server"}},
                                          tripped.value_or("ReceiveMessagesOnPollGroup returned -1")));
    }
    if (count == 0) {
      break;
    }
    for (int i = 0; i < count; ++i) {
      const auto* bytes = static_cast<const std::byte*>(incoming[i]->m_pData);
      messages.push_back(PeerMessage{.from = static_cast<PeerId>(incoming[i]->m_conn),
                                     .payload = Payload(bytes, bytes + incoming[i]->m_cbSize)});
#if AUGUSTA_DEBUG_TOOLS
      if (impl_->tap != nullptr) {
        ReportToTap(impl_->tap, TapDirection::kInbound, impl_->local, RemoteTapEndpoint(incoming[i]->m_conn),
                    messages.back().payload);
      }
#endif
      incoming[i]->Release();
    }
  }
  if (!messages.empty()) {
    LOG_TRACE("subsystem=networking event=receive role=server count={}", messages.size());
  }
  return messages;
}

std::vector<PeerStats> Server::GetStats() const {
  std::vector<HSteamNetConnection> peers;
  {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    peers = impl_->peers.Connected();
  }
  std::vector<PeerStats> stats;
  stats.reserve(peers.size());
  for (const HSteamNetConnection connection : peers) {
    if (const std::optional<ConnectionStats> connection_stats = GetConnectionStats(connection)) {
      stats.push_back(PeerStats{.peer = static_cast<PeerId>(connection), .stats = *connection_stats});
    }
  }
  return stats;
}

}  // namespace augusta::networking
