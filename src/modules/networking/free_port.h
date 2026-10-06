#ifndef AUGUSTA_NETWORKING_FREE_PORT_H_
#define AUGUSTA_NETWORKING_FREE_PORT_H_

#include <cstdint>
#include <optional>

/// \file
/// Where a server bound to port 0 listens, private to the module:
/// GameNetworkingSockets refuses port 0, so networking.cpp asks this for one.
namespace augusta::networking {

/// A UDP port no socket on this machine holds, as the OS picks for a socket
/// bound to port 0 at the wildcard address of IPv6 if ipv6, else of IPv4; or
/// nullopt if the OS would not give one. The port is free only as of the call.
///
/// Why not any port that binds: a client's socket is bound to the wildcard
/// address on a port the OS picks, and on Windows a later bind to a specific
/// address (127.0.0.1) on that port succeeds and takes every packet sent to
/// it, leaving that client deaf to its own server. The OS never picks a port a
/// wildcard socket holds. A client handed this same port in the instant before
/// its caller binds it would still lose it, unnoticed: the OS picking that one
/// port again, that soon, is what this leaves to chance.
std::optional<std::uint16_t> FreeUdpPort(bool ipv6);

}  // namespace augusta::networking

#endif  // AUGUSTA_NETWORKING_FREE_PORT_H_
