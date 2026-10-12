#ifndef AUGUSTA_SHARED_WIRE_H_
#define AUGUSTA_SHARED_WIRE_H_

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/tick.h"

/// \file
/// The conversions both peers make between the Networking Protocol's plain
/// types and the shared core's (ADR-0038): a Command, a stance and a pack's
/// hash travel the same way whichever side sends them, so they are converted
/// here once, for server/wire.h and the client's augusta/harness_wire.h to
/// call. Each peer's own types (its sessions, its Match, its Authoritative
/// State) stay with its own adapter. Pure field-by-field copies.
namespace augusta::wire {

/// stance as the protocol carries it.
[[nodiscard]] protocol::StanceWire ToWire(physics::Stance stance);

/// stance in the engine's terms.
[[nodiscard]] physics::Stance FromWire(protocol::StanceWire stance);

/// command as the protocol carries it in a message whose Seen tick is
/// seen_tick: its own Seen time's tick as how far before that it is, no further
/// than a byte tells (0 when seen_tick is not after it).
[[nodiscard]] protocol::CommandWire ToWire(const command::Command& command, tick::Tick seen_tick);

/// command in the engine's terms, its Seen time's tick seen_age before
/// seen_tick (never before tick 0).
[[nodiscard]] command::Command FromWire(const protocol::CommandWire& command, tick::Tick seen_tick);

/// hash as the protocol carries it.
[[nodiscard]] protocol::PackHashWire ToWire(const assets::PackHash& hash);

/// hash in the engine's terms.
[[nodiscard]] assets::PackHash FromWire(const protocol::PackHashWire& hash);

}  // namespace augusta::wire

#endif  // AUGUSTA_SHARED_WIRE_H_
