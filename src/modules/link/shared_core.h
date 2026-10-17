#ifndef AUGUSTA_LINK_SHARED_CORE_H_
#define AUGUSTA_LINK_SHARED_CORE_H_

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/protocol.h"
#include "augusta/tick.h"

/// \file
/// The shared core's own types as the protocol carries them, in a message and
/// in a Match capture's record alike: a Command and a pack's hash. Private to
/// the link module, for augusta/link_messages.h's two halves.
namespace augusta::link {

/// command as the protocol carries it in a message whose Seen tick is
/// seen_tick: its own Seen time's tick as how far before that it is, no further
/// than a byte tells (0 when seen_tick is not after it).
[[nodiscard]] protocol::CommandWire ToWire(const command::Command& command, tick::Tick seen_tick);

/// command in the engine's terms, its Seen time's tick seen_age before
/// seen_tick (never before tick 0).
[[nodiscard]] command::Command FromWire(const protocol::CommandWire& command, tick::Tick seen_tick);

[[nodiscard]] protocol::PackHashWire ToWire(const assets::PackHash& hash);
[[nodiscard]] assets::PackHash FromWire(const protocol::PackHashWire& hash);

}  // namespace augusta::link

#endif  // AUGUSTA_LINK_SHARED_CORE_H_
