#ifndef AUGUSTA_PRIMITIVES_H_
#define AUGUSTA_PRIMITIVES_H_

#include <cstddef>
#include <cstdint>

/// \file
/// augusta::primitives holds the counters and bounds both the engine's modules
/// and the Networking Protocol (augusta::protocol, ADR-0038) need. Neither side
/// owns them, so they live below both, as the grids do in augusta/grid.h: the protocol can encode
/// them without depending on the Command, the tick schedule or the Parameters,
/// and those can bound their own data without depending on the protocol.
///
/// It depends on nothing but the standard library, and must stay that way; and
/// nothing else defines these again, but takes them from here
/// (tests/core_boundary.cmake).
namespace augusta::primitives {

/// A server Tick's number: from 1 for the life of the server process, never
/// starting over, so wide enough never to wrap (32 bits would after about 828
/// days at 60 Hz).
using Tick = std::uint64_t;

/// The number a client gives each Command it sends, and the acknowledged
/// sequence that answers it: from 1, one more per command, over one connection.
/// As wide as Tick, so it never wraps either and receivers order sequences as
/// plain numbers.
using Sequence = std::uint64_t;

/// The players a Lobby or a Match holds, and so the most a Lobby, a Match start,
/// an Authoritative State update or a Parameters' Player count may name.
inline constexpr std::size_t kMaxPlayers = 8;

/// The most commands one Commands message carries: a client repeats at most
/// this many of its newest unacknowledged ones.
inline constexpr std::size_t kMaxCommandsPerMessage = 8;

/// The most kicks a rifle's recoil pattern in the Parameters holds.
inline constexpr std::size_t kMaxRecoilKicks = 64;

}  // namespace augusta::primitives

#endif  // AUGUSTA_PRIMITIVES_H_
