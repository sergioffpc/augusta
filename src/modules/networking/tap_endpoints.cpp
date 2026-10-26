#include "tap_endpoints.h"

#if AUGUSTA_DEBUG_TOOLS
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

#include <steam/isteamnetworkingsockets.h>
#include <steam/steamnetworkingtypes.h>

#include "augusta/networking.h"

namespace augusta::networking {

TapEndpoint ToTapEndpoint(const SteamNetworkingIPAddr& addr) {
  TapEndpoint endpoint{.address = Ipv4Address{}, .port = addr.m_port};
  if (addr.IsIPv4()) {
    const std::uint32_t ipv4 = addr.GetIPv4();
    constexpr unsigned kByte = 8;
    endpoint.address =
        Ipv4Address{static_cast<std::uint8_t>(ipv4 >> (3 * kByte)), static_cast<std::uint8_t>(ipv4 >> (2 * kByte)),
                    static_cast<std::uint8_t>(ipv4 >> kByte), static_cast<std::uint8_t>(ipv4)};
  } else {
    Ipv6Address ipv6;
    std::ranges::copy(addr.m_ipv6, ipv6.begin());
    endpoint.address = ipv6;
  }
  return endpoint;
}

TapEndpoint RemoteTapEndpoint(HSteamNetConnection connection) {
  SteamNetConnectionInfo_t info;
  if (!SteamNetworkingSockets()->GetConnectionInfo(connection, &info)) {
    return {};
  }
  return ToTapEndpoint(info.m_addrRemote);
}

void ReportToTap(Tap* tap, TapDirection direction, const TapEndpoint& local, const TapEndpoint& remote,
                 std::span<const std::byte> payload) {
  if (tap == nullptr) {
    return;
  }
  tap->Report(TappedMessage{.direction = direction,
                            .local = local,
                            .remote = remote,
                            .payload = payload,
                            .time = std::chrono::system_clock::now()});
}

TapEndpoint UnspecifiedEndpointLike(const TapEndpoint& endpoint) {
  if (std::holds_alternative<Ipv6Address>(endpoint.address)) {
    return TapEndpoint{.address = Ipv6Address{}, .port = 0};
  }
  return TapEndpoint{.address = Ipv4Address{}, .port = 0};
}

}  // namespace augusta::networking
#endif
