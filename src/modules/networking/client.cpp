#include <algorithm>
#include <array>
#include <cstddef>
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

struct Client::Impl {
  explicit Impl(failure::Faults* faults) : faults(faults) {}

  // Null outside tests.
  failure::Faults* const faults;
  // Guards connection and state, and is held across every transport call on
  // the connection, so none of them can race the call that closes it: a
  // connection handle the transport calls invalid is then its own failure.
  std::mutex mutex;
  HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
  ConnectionState state = ConnectionState::kDisconnected;
  // What arrived on a connection the server ended, taken off it before it
  // was closed, for the next ReceiveMessages: the server sent it before
  // closing, often to say why. Guarded by mutex.
  std::vector<Payload> received_before_close;
  // Filled by the status-changed callback, drained by PumpEvents.
  TransportEventQueue transport_events;
#if AUGUSTA_DEBUG_TOOLS
  // Null unless a Tap was given; remote is the server last connected to and
  // local the unspecified address in its family (GNS gives none), guarded by
  // mutex.
  Tap* tap = nullptr;
  TapEndpoint local;
  TapEndpoint remote;
#endif

  // ReceiveBatch on connection under mutex, which the caller holds,
  // reporting each message it took off the transport to the tap.
  int ReceiveBatchLocked(std::vector<Payload>& into) const {
    const int count = ReceiveBatch(connection, into);
#if AUGUSTA_DEBUG_TOOLS
    for (std::size_t index = into.size() - static_cast<std::size_t>(std::max(count, 0)); index < into.size(); ++index) {
      ReportToTap(tap, TapDirection::kInbound, local, remote, into[index]);
    }
#endif
    return count;
  }

  // Applies one drained event: the state under the lock, then the transport
  // call it needs and its log line outside it.
  void Apply(const TransportEvent& event) {
    ClientTransition transition;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      transition = ApplyToClient(connection, state, event);
      state = transition.state;
      if (transition.ended) {
        // Closing the connection releases what it still holds unread.
        while (ReceiveBatchLocked(received_before_close) > 0) {
        }
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

Client::Client(failure::Faults* faults) : impl_(std::make_unique<Impl>(faults)) {}

#if AUGUSTA_DEBUG_TOOLS
Client::Client(failure::Faults* faults, Tap* tap) : Client(faults) { impl_->tap = tap; }
#endif

Client::~Client() { Disconnect(); }

void Client::Connect(const Endpoint& server) {
  SteamNetworkingIPAddr addr;
  addr.Clear();
  if (!addr.ParseString(server.address.c_str())) {
    throw UnparsedAddress(server.address);
  }
  LOG_INFO("subsystem=networking event=connecting role=client server_addr={}", server.address);

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
  // A connection's leftovers are never read as the next one's.
  impl_->received_before_close.clear();
#if AUGUSTA_DEBUG_TOOLS
  impl_->remote = ToTapEndpoint(addr);
  impl_->local = UnspecifiedEndpointLike(impl_->remote);
#endif
  impl_->connection =
      SteamNetworkingSockets()->ConnectByIPAddress(addr, static_cast<int>(options.size()), options.data());
  // A connection GNS refused to create never gets a status-changed callback,
  // so this is the only place that can report it.
  if (impl_->connection == k_HSteamNetConnection_Invalid) {
    impl_->state = ConnectionState::kDisconnected;
    LOG_WARN(
        "subsystem=networking event=state_changed role=client state=disconnected reason=connection_not_created "
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
  // Closing it here is the caller's choice: what is left unread goes too.
  impl_->received_before_close.clear();
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

  return GetConnectionStats(impl_->connection);
}

SendResult Client::Send(const Payload& payload, Reliability reliability) {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->state != ConnectionState::kConnected) {
    return SendOutcome::kDropped;
  }
  const Sent sent = SendToConnection(impl_->faults, impl_->connection, payload, reliability);
  switch (ClassifySend(sent.result, reliability)) {
    case SendVerdict::kAccepted:
      LOG_TRACE("subsystem=networking event=send role=client bytes={}", payload.size());
#if AUGUSTA_DEBUG_TOOLS
      ReportToTap(impl_->tap, TapDirection::kOutbound, impl_->local, impl_->remote, payload);
#endif
      return SendOutcome::kAccepted;
    case SendVerdict::kDropped:
      return SendOutcome::kDropped;
    case SendVerdict::kDroppedEndingConnection:
      // Its status-changed events are dropped with it: the client forgets it here.
      LOG_WARN("subsystem=networking event=state_changed role=client state=disconnected reason=send_queue_full");
      SteamNetworkingSockets()->CloseConnection(impl_->connection, 0, nullptr, false);
      impl_->connection = k_HSteamNetConnection_Invalid;
      impl_->state = ConnectionState::kDisconnected;
      return SendOutcome::kDropped;
    case SendVerdict::kLocalFailure:
      break;
  }
  return std::unexpected(
      LocalFailure(failure::Code::kTransportSendFailed, {{.key = "role", .value = "client"}}, sent.Detail()));
}

std::expected<std::vector<Payload>, failure::Failure> Client::ReceiveMessages() {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->connection == k_HSteamNetConnection_Invalid) {
    return std::exchange(impl_->received_before_close, {});
  }

  std::vector<Payload> messages;
  while (true) {
    std::optional<std::string> tripped = ReceiveTripped(impl_->faults);
    const int count = tripped.has_value() ? -1 : impl_->ReceiveBatchLocked(messages);
    if (count < 0) {
      return std::unexpected(LocalFailure(failure::Code::kTransportReceiveFailed, {{.key = "role", .value = "client"}},
                                          tripped.value_or("ReceiveMessagesOnConnection returned -1")));
    }
    if (count == 0) {
      break;
    }
  }
  if (!messages.empty()) {
    LOG_TRACE("subsystem=networking event=receive role=client count={}", messages.size());
  }
  return messages;
}

}  // namespace augusta::networking
