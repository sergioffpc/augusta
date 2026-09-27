#ifndef AUGUSTA_NETWORKING_SEND_FLAGS_H_
#define AUGUSTA_NETWORKING_SEND_FLAGS_H_

#include <steam/steamnetworkingtypes.h>

#include "augusta/networking.h"

// Decision half of every send, private to the module so it is tested apart
// from a live connection: networking.cpp hands the result to
// GameNetworkingSockets.
namespace augusta::networking {

/// The GameNetworkingSockets send flags for a message sent as reliability says.
// An unreliable message goes out without Nagle: the transport would otherwise
// hold it back for its batching window (about 5 ms), and a Command or an
// Authoritative State update is stale by then. A reliable one (the join, the
// Lobby, Match start) keeps Nagle: a few milliseconds more do not matter to
// it, and messages sent together may share a packet. The choice is the
// transport's (ADR-0003): Reliability keeps meaning only how a message is
// delivered.
constexpr int SendFlags(Reliability reliability) {
  return reliability == Reliability::kReliable ? k_nSteamNetworkingSend_Reliable
                                               : k_nSteamNetworkingSend_UnreliableNoNagle;
}

}  // namespace augusta::networking

#endif  // AUGUSTA_NETWORKING_SEND_FLAGS_H_
