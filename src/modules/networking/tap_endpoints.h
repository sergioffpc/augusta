#ifndef AUGUSTA_NETWORKING_TAP_ENDPOINTS_H_
#define AUGUSTA_NETWORKING_TAP_ENDPOINTS_H_

#include <cstddef>
#include <span>

#include <steam/steamnetworkingtypes.h>

#include "augusta/networking.h"

/// \file
/// What Client and Server report to a Tap (ADR-0074), private to the module
/// and compiled only in a Development build: GameNetworkingSockets' addresses
/// as the tap's plain endpoints, and the report of one message.
#if AUGUSTA_DEBUG_TOOLS
namespace augusta::networking {

/// addr as the tap's plain value. GameNetworkingSockets holds an IPv4 address
/// as an IPv4-mapped IPv6 one, which the tap reports as the IPv4 it maps.
TapEndpoint ToTapEndpoint(const SteamNetworkingIPAddr& addr);

/// connection's remote end, unspecified if the transport has none for it (it is gone).
TapEndpoint RemoteTapEndpoint(HSteamNetConnection connection);

/// Reports one message to tap, if there is one, timed now.
void ReportToTap(Tap* tap, TapDirection direction, const TapEndpoint& local, const TapEndpoint& remote,
                 std::span<const std::byte> payload);

}  // namespace augusta::networking
#endif

#endif  // AUGUSTA_NETWORKING_TAP_ENDPOINTS_H_
