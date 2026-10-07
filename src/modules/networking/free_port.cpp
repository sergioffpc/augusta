#include "free_port.h"

#include <cstdint>
#include <optional>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace augusta::networking {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
void CloseSocket(NativeSocket socket) { closesocket(socket); }
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
void CloseSocket(NativeSocket socket) { close(socket); }
#endif

// Binds socket to port 0 at the wildcard address of its family, and returns the
// port the OS gave it.
std::optional<std::uint16_t> BindAnyPort(NativeSocket socket, bool ipv6) {
  sockaddr_storage addr{};
  socklen_t length = 0;
  if (ipv6) {
    auto& addr6 = reinterpret_cast<sockaddr_in6&>(addr);
    addr6.sin6_family = AF_INET6;
    addr6.sin6_addr = in6addr_any;
    length = sizeof(addr6);
  } else {
    auto& addr4 = reinterpret_cast<sockaddr_in&>(addr);
    addr4.sin_family = AF_INET;
    addr4.sin_addr.s_addr = htonl(INADDR_ANY);
    length = sizeof(addr4);
  }
  if (bind(socket, reinterpret_cast<const sockaddr*>(&addr), length) != 0 ||
      getsockname(socket, reinterpret_cast<sockaddr*>(&addr), &length) != 0) {
    return std::nullopt;
  }
  return ntohs(ipv6 ? reinterpret_cast<const sockaddr_in6&>(addr).sin6_port
                    : reinterpret_cast<const sockaddr_in&>(addr).sin_port);
}

}  // namespace

std::optional<std::uint16_t> FreeUdpPort(bool ipv6) {
#ifdef _WIN32
  // Counted, so it neither disturbs nor depends on GameNetworkingSockets' own.
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    return std::nullopt;
  }
#endif
  std::optional<std::uint16_t> port;
  if (const NativeSocket socket = ::socket(ipv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      socket != kInvalidSocket) {
    port = BindAnyPort(socket, ipv6);
    CloseSocket(socket);
  }
#ifdef _WIN32
  WSACleanup();
#endif
  return port;
}

}  // namespace augusta::networking
