#ifndef AUGUSTA_REPLICATION_H_
#define AUGUSTA_REPLICATION_H_

#include <cstdint>
#include <span>
#include <vector>

#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/simulation.h"

// augusta::replication decides what SimulationWorld's per-tick Authoritative
// State (augusta::simulation::State, ADR-0023) means for each connected
// client: which of them is sent what. It stops at what each is sent, in the
// engine's own types; turning that into a message, encoding it and putting it
// on the wire is the caller's mechanism (server::Host), so the decision is a
// pure function tested without a network (docs/agents/coding-standards.md).
namespace augusta::replication {

/// SimulationWorld names a player by the number of its session, so a state can
/// be sent back under the names clients know.
[[nodiscard]] simulation::PlayerId PlayerOf(protocol::SessionIdWire session);

/// The session a SimulationWorld player belongs to; the inverse of PlayerOf.
[[nodiscard]] protocol::SessionIdWire SessionOf(simulation::PlayerId player);

/// One connected client a tick's state is for.
struct Recipient {
  protocol::SessionIdWire session{};
  /// The highest command sequence of this client that the tick processed, 0 if none.
  std::uint32_t acknowledged_sequence = 0;
};

/// One player's body, under the session clients know it by.
struct PlayerBody {
  protocol::SessionIdWire session{};
  physics::BodyState body{};
};

/// What one recipient is sent for a tick.
struct Update {
  protocol::SessionIdWire recipient{};
  /// The server tick the bodies are from.
  std::uint32_t tick = 0;
  /// The highest command sequence of the recipient that the tick processed, 0 if none.
  std::uint32_t acknowledged_sequence = 0;
  /// Every player in the match.
  std::vector<PlayerBody> players;
};

/// What each recipient is sent for tick: every player's body, and its own
/// acknowledged sequence (which is why each update is its own message).
[[nodiscard]] std::vector<Update> PlanUpdates(const simulation::State& state, std::uint32_t tick,
                                              std::span<const Recipient> recipients);

}  // namespace augusta::replication

#endif  // AUGUSTA_REPLICATION_H_
