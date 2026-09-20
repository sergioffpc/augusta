#ifndef AUGUSTA_REPLICATION_H_
#define AUGUSTA_REPLICATION_H_

#include <cstdint>
#include <span>
#include <vector>

#include "augusta/protocol.h"
#include "augusta/simulation.h"

// augusta::replication decides what SimulationWorld's per-tick Authoritative
// State (augusta::simulation::State, ADR-0023) means for each connected
// client: which of them is sent what. It stops at the message
// (protocol::AuthoritativeState); encoding it and putting it on the wire is
// the caller's mechanism (server::Host), so the decision is a pure function
// tested without a network (docs/agents/coding-standards.md).
namespace augusta::replication {

/// SimulationWorld names a player by the number of its session, so a state can
/// be sent back under the names clients know.
[[nodiscard]] simulation::PlayerId PlayerOf(protocol::SessionId session);

/// The session a SimulationWorld player belongs to; the inverse of PlayerOf.
[[nodiscard]] protocol::SessionId SessionOf(simulation::PlayerId player);

/// One connected client a tick's state is for.
struct Recipient {
  protocol::SessionId session{};
  /// The highest command sequence of this client that the tick processed, 0 if none.
  std::uint32_t acknowledged_sequence = 0;
};

/// The message one recipient is sent.
struct Update {
  protocol::SessionId recipient{};
  protocol::AuthoritativeState state;
};

/// What each recipient is sent for tick: every player's body, and its own
/// acknowledged sequence (which is why each update is its own message).
[[nodiscard]] std::vector<Update> PlanUpdates(const simulation::State& state, std::uint32_t tick,
                                              std::span<const Recipient> recipients);

}  // namespace augusta::replication

#endif  // AUGUSTA_REPLICATION_H_
