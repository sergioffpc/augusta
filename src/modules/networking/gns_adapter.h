#ifndef AUGUSTA_NETWORKING_GNS_ADAPTER_H_
#define AUGUSTA_NETWORKING_GNS_ADAPTER_H_

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <steam/steamclientpublic.h>
#include <steam/steamnetworkingtypes.h>

#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/networking.h"
#include "transport_events.h"

/// \file
/// What Client and Server share over GameNetworkingSockets, private to the
/// module: its types turned into the module's (addresses, stats, events,
/// failures), and the transport calls both roles make the same way - a send, a
/// connection's receive, a listen socket, the call an event asks for.
namespace augusta::networking {

/// How many messages a receive pulls out of GNS per underlying call - just a
/// batch size for draining the queue in a receive's own loop, not a cap on how
/// many messages can be received overall.
inline constexpr int kMaxMessagesPerBatch = 16;

/// addr as "ip:port". Peer IPs are fine to log (ADR-0029) - direct-IP
/// connections, no matchmaking/relay to anonymize, so the server already sees them.
std::string FormatAddr(const SteamNetworkingIPAddr& addr);

/// connection's stats, nullopt if the transport has none for it (it is gone).
std::optional<ConnectionStats> GetConnectionStats(HSteamNetConnection connection);

/// What a status-changed callback publishes: the minimal event, and nothing else.
TransportEvent ToTransportEvent(const SteamNetConnectionStatusChangedCallback_t& info);

/// The one log line for a connection's state change, written by the Network I/O
/// owner as it applies the event, not by the callback.
void LogTransportEvent(std::string_view role, const TransportEvent& event);

/// Makes call for connection: the transport work an event asked for, run by the
/// Network I/O owner with no lock held. Returns false only if the connection
/// could not join the poll group.
bool MakeTransportCall(TransportCall call, HSteamNetConnection connection, HSteamNetPollGroup poll_group);

/// A failure of the local transport, of code, at what it happened to.
failure::Failure LocalFailure(failure::Code code, std::vector<failure::ContextField> context, std::string detail);

/// What a constructor or Connect throws for an address that does not parse: the
/// config named it, so it is the config's failure.
failure::ClassifiedFailure UnparsedAddress(const std::string& address);

/// One message sent to a connection, as what SendMessageToConnection answers,
/// or as k_EResultFail - the transport failing - with the armed fault's detail
/// when kTransportSend is armed.
struct Sent {
  EResult result = k_EResultOK;
  std::optional<std::string> injected;

  /// What the transport said, for a local failure's detail.
  [[nodiscard]] std::string Detail() const;
};

/// Sends payload to connection as reliability says, unless faults (null
/// outside tests) has kTransportSend armed.
Sent SendToConnection(failure::Faults* faults, HSteamNetConnection connection, const Payload& payload,
                      Reliability reliability);

/// The detail of a fault armed at kTransportReceive, if faults has one, for a
/// receive to fail as the transport's does (returns -1).
std::optional<std::string> ReceiveTripped(failure::Faults* faults);

/// Appends to into the next batch of messages received on connection; returns
/// how many, or -1 if the transport failed to receive.
int ReceiveBatch(HSteamNetConnection connection, std::vector<Payload>& into);

/// Listens on addr with options. GameNetworkingSockets refuses port 0, so for
/// it this listens on a port the OS reports free (see FreeUdpPort) - asking
/// again if it fails to bind there - and leaves the one it bound in addr.
HSteamListenSocket CreateListenSocket(SteamNetworkingIPAddr& addr,
                                      std::span<const SteamNetworkingConfigValue_t> options);

}  // namespace augusta::networking

#endif  // AUGUSTA_NETWORKING_GNS_ADAPTER_H_
