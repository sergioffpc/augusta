#ifndef AUGUSTA_LINK_PEER_MESSAGE_H_
#define AUGUSTA_LINK_PEER_MESSAGE_H_

#include <cstddef>
#include <span>

#include "augusta/link.h"

/// \file
/// What a payload a peer sent is to its server, private to the module so it is
/// tested apart from a live connection.
namespace augusta::link {

/// payload, which from sent, decoded: its size, its type or why it has none,
/// and what it says if it is a client's message.
[[nodiscard]] PeerMessage ReadFrom(PeerId from, std::span<const std::byte> payload);

}  // namespace augusta::link

#endif  // AUGUSTA_LINK_PEER_MESSAGE_H_
