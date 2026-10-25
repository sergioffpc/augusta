#include "free_port.h"

#include <cstdint>
#include <optional>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/system/error_code.hpp>

namespace augusta::networking {

std::optional<std::uint16_t> FreeUdpPort(bool ipv6) {
  using Udp = boost::asio::ip::udp;
  const Udp protocol = ipv6 ? Udp::v6() : Udp::v4();
  boost::asio::io_context io;
  Udp::socket socket(io);
  // Port 0 at the wildcard address of its family, for the OS to pick one.
  boost::system::error_code error;
  socket.open(protocol, error);
  if (!error) {
    socket.bind(Udp::endpoint(protocol, 0), error);
  }
  const Udp::endpoint bound = error ? Udp::endpoint() : socket.local_endpoint(error);
  if (error) {
    return std::nullopt;
  }
  return bound.port();
}

}  // namespace augusta::networking
