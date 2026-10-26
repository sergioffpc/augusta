#include "gns_adapter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>  // SteamNetworkingIPAddr::ToString's inline body lives here.
#include <steam/steamclientpublic.h>
#include <steam/steamnetworkingtypes.h>

#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "free_port.h"
#include "send_flags.h"
#include "transport_events.h"

namespace augusta::networking {

namespace {

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

}  // namespace

std::string FormatAddr(const SteamNetworkingIPAddr& addr) {
  std::array<char, SteamNetworkingIPAddr::k_cchMaxString> buf;
  addr.ToString(buf.data(), buf.size(), /*bWithPort=*/true);
  return buf.data();
}

std::optional<ConnectionStats> GetConnectionStats(HSteamNetConnection connection) {
  SteamNetConnectionRealTimeStatus_t status;
  if (SteamNetworkingSockets()->GetConnectionRealTimeStatus(connection, &status, 0, nullptr) != k_EResultOK) {
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

TransportEvent ToTransportEvent(const SteamNetConnectionStatusChangedCallback_t& info) {
  return TransportEvent{.connection = info.m_hConn,
                        .state = ToTransportState(info.m_info.m_eState),
                        .remote_address = FormatAddr(info.m_info.m_addrRemote)};
}

void LogTransportEvent(std::string_view role, const TransportEvent& event) {
  switch (event.state) {
    case TransportState::kConnecting:
      LOG_DEBUG("subsystem=networking event=state_changed role={} state=connecting peer={} peer_addr={}", role,
                event.connection, event.remote_address);
      break;
    case TransportState::kConnected:
      LOG_INFO("subsystem=networking event=state_changed role={} state=connected peer={} peer_addr={}", role,
               event.connection, event.remote_address);
      break;
    case TransportState::kClosedByPeer:
      LOG_INFO(
          "subsystem=networking event=state_changed role={} state=disconnected peer={} peer_addr={} "
          "reason=closed_by_peer",
          role, event.connection, event.remote_address);
      break;
    case TransportState::kProblemDetectedLocally:
      LOG_WARN(
          "subsystem=networking event=state_changed role={} state=disconnected peer={} peer_addr={} "
          "reason=problem_detected_locally",
          role, event.connection, event.remote_address);
      break;
    case TransportState::kOther:
      break;
  }
}

bool MakeTransportCall(TransportCall call, HSteamNetConnection connection, HSteamNetPollGroup poll_group) {
  switch (call) {
    case TransportCall::kNone:
      break;
    case TransportCall::kJoinPollGroup:
      return SteamNetworkingSockets()->SetConnectionPollGroup(connection, poll_group);
    case TransportCall::kClose:
      SteamNetworkingSockets()->CloseConnection(connection, 0, nullptr, false);
      break;
  }
  return true;
}

failure::Failure LocalFailure(failure::Code code, std::vector<failure::ContextField> context, std::string detail) {
  return failure::Failure{.code = code, .context = std::move(context), .detail = std::move(detail)};
}

failure::ClassifiedFailure UnparsedAddress(const std::string& address) {
  return failure::ClassifiedFailure(failure::Failure{.code = failure::Code::kInvalidConfiguration,
                                                     .context = {{.key = "address", .value = address}},
                                                     .detail = "not a numeric host:port"});
}

std::string Sent::Detail() const {
  return injected.value_or(std::format("SendMessageToConnection returned EResult {}", static_cast<int>(result)));
}

Sent SendToConnection(failure::Faults* faults, HSteamNetConnection connection, const Payload& payload,
                      Reliability reliability) {
  if (faults != nullptr) {
    if (std::optional<std::string> tripped = faults->Trip(failure::Site::kTransportSend)) {
      return Sent{.result = k_EResultFail, .injected = std::move(tripped)};
    }
  }
  return Sent{
      .result = SteamNetworkingSockets()->SendMessageToConnection(
          connection, payload.data(), static_cast<std::uint32_t>(payload.size()), SendFlags(reliability), nullptr),
      .injected = std::nullopt};
}

std::optional<std::string> ReceiveTripped(failure::Faults* faults) {
  return faults != nullptr ? faults->Trip(failure::Site::kTransportReceive) : std::nullopt;
}

int ReceiveBatch(HSteamNetConnection connection, std::vector<Payload>& into) {
  std::array<ISteamNetworkingMessage*, kMaxMessagesPerBatch> incoming;
  const int count =
      SteamNetworkingSockets()->ReceiveMessagesOnConnection(connection, incoming.data(), kMaxMessagesPerBatch);
  for (int i = 0; i < count; ++i) {
    const auto* bytes = static_cast<const std::byte*>(incoming[i]->m_pData);
    into.emplace_back(bytes, bytes + incoming[i]->m_cbSize);
    incoming[i]->Release();
  }
  return count;
}

HSteamListenSocket CreateListenSocket(SteamNetworkingIPAddr& addr,
                                      std::span<const SteamNetworkingConfigValue_t> options) {
  const auto create = [&] {
    return SteamNetworkingSockets()->CreateListenSocketIP(addr, static_cast<int>(options.size()), options.data());
  };
  if (addr.m_port != 0) {
    return create();
  }
  // A port fails to bind only if a socket took it in the instant between the
  // two, so a single attempt all but always binds.
  constexpr int kAttempts = 8;
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    const std::optional<std::uint16_t> port = FreeUdpPort(!addr.IsIPv4());
    if (!port.has_value()) {
      break;
    }
    addr.m_port = *port;
    if (const HSteamListenSocket socket = create(); socket != k_HSteamListenSocket_Invalid) {
      return socket;
    }
  }
  return k_HSteamListenSocket_Invalid;
}

}  // namespace augusta::networking
