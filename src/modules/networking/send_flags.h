#ifndef AUGUSTA_NETWORKING_SEND_FLAGS_H_
#define AUGUSTA_NETWORKING_SEND_FLAGS_H_

#include <cstdint>

#include <steam/steamclientpublic.h>
#include <steam/steamnetworkingtypes.h>

#include "augusta/networking.h"

/// \file
/// Decision half of every send, private to the module so it is tested apart
/// from a live connection: which flags networking.cpp hands
/// GameNetworkingSockets a message with, and what the result it answers means
/// (ADR-0033): accepted, a peer outcome, or a failure of the local transport.
namespace augusta::networking {

/// The GameNetworkingSockets send flags for a message sent as reliability says.
/// An unreliable message goes out without Nagle: the transport would otherwise
/// hold it back for its batching window (about 5 ms), and a Command or an
/// Authoritative State update is stale by then. A reliable one (the join, the
/// Lobby, Match start) keeps Nagle: a few milliseconds more do not matter to
/// it, and messages sent together may share a packet. The choice is the
/// transport's (ADR-0003): Reliability keeps meaning only how a message is
/// delivered.
constexpr int SendFlags(Reliability reliability) {
  return reliability == Reliability::kReliable ? k_nSteamNetworkingSend_Reliable
                                               : k_nSteamNetworkingSend_UnreliableNoNagle;
}

/// What one send's result means.
enum class SendVerdict : std::uint8_t {
  /// The transport took the message.
  kAccepted,
  /// The peer's connection could not take it (not connected, ending, or a full
  /// queue of an unreliable message): dropped, the peer's own outcome.
  kDropped,
  /// A reliable message the peer's full queue could not take: dropped, and the
  /// connection ends, since reliable delivery can no longer be kept.
  kDroppedEndingConnection,
  /// The local transport failed: the runtime's failure, not the peer's.
  kLocalFailure,
};

/// What SendMessageToConnection's result means for a message sent as
/// reliability says. The caller holds the connection open across the call (no
/// close can race it), so an invalid handle is the local transport's failure
/// too.
constexpr SendVerdict ClassifySend(EResult result, Reliability reliability) {
  switch (result) {
    case k_EResultOK:
      return SendVerdict::kAccepted;
    case k_EResultNoConnection:
    case k_EResultInvalidState:
    case k_EResultIgnored:
      return SendVerdict::kDropped;
    case k_EResultLimitExceeded:
      return reliability == Reliability::kReliable ? SendVerdict::kDroppedEndingConnection : SendVerdict::kDropped;
    default:
      return SendVerdict::kLocalFailure;
  }
}

}  // namespace augusta::networking

#endif  // AUGUSTA_NETWORKING_SEND_FLAGS_H_
