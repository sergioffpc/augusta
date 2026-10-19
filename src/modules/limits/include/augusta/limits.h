#ifndef AUGUSTA_LIMITS_H_
#define AUGUSTA_LIMITS_H_

#include <cstddef>

/// \file
/// augusta::limits holds the bounds both the gameplay rules and the Networking
/// Protocol (augusta::protocol, ADR-0038) need. Neither side owns them, so they
/// live below both: the protocol can encode them without depending on the
/// Parameters, and the Parameters can bound their own data without depending on
/// the protocol.
///
/// It depends on nothing but the standard library, and must stay that way; and
/// nothing else defines these again, but takes them from here
/// (checks/core_boundary.cmake).
namespace augusta::limits {

/// The players a Lobby or a Match holds, and so the most a Lobby, a Match start,
/// an Authoritative State update or a Parameters' Player count may name.
inline constexpr std::size_t kMaxPlayers = 8;

/// The most kicks a rifle's recoil pattern in the Parameters holds.
inline constexpr std::size_t kMaxRecoilKicks = 64;

}  // namespace augusta::limits

#endif  // AUGUSTA_LIMITS_H_
