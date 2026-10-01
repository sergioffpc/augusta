#ifndef AUGUSTA_REPLICATION_H_
#define AUGUSTA_REPLICATION_H_

#include <cstdint>
#include <span>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/weapon.h"

// augusta::replication decides what SimulationWorld's per-tick Authoritative
// State (augusta::simulation::State, ADR-0023) means for each connected
// client: which of them is sent what. It stops at what each is sent, in the
// engine's own types; turning that into a message, encoding it and putting it
// on the wire is the caller's mechanism (server::Host), so the decision is a
// pure function tested without a network (docs/agents/coding-standards.md).
// It names every body as SimulationWorld does (simulation::EntityId), and each
// recipient by the entity its player controls; which session and connection
// that is, is the caller's to say.
namespace augusta::replication {

/// One connected client a tick's state is for, by the entity its player controls.
struct Recipient {
  simulation::EntityId entity{};
  /// The highest command sequence of this client that the tick processed, 0 if none.
  std::uint32_t acknowledged_sequence = 0;
  /// How many of this client's commands the server still holds queued after the tick.
  std::uint8_t queued_commands = 0;
};

/// One dynamic body. Its health is not among what a client is told of it.
struct EntityBody {
  simulation::EntityId entity{};
  physics::BodyState body{};
  /// Where the body faces, as a yaw in radians.
  float yaw = 0.0F;
};

/// What one recipient is sent for a tick.
struct Update {
  /// The entity the recipient's player controls.
  simulation::EntityId recipient{};
  /// The server tick the bodies are from.
  tick::Tick tick = 0;
  /// The highest command sequence of the recipient that the tick processed, 0 if none.
  std::uint32_t acknowledged_sequence = 0;
  /// Every dynamic body in the match.
  std::vector<EntityBody> bodies;
  /// The recipient's own rifle after the tick, for it to reconcile its
  /// predicted one against; no one else's is sent. A rifle with no round if the
  /// recipient has no body in the state.
  weapon::State rifle{};
  /// The recipient's own health after the tick; no one else's is sent. 0 if the
  /// recipient has no body in the state: it has died.
  float health = 0.0F;
  /// How many of the recipient's commands the server still holds queued after the tick.
  std::uint8_t queued_commands = 0;
};

/// What each recipient is sent for tick: every body, and its own acknowledged
/// sequence, rifle, health and queued commands (which is why each update is
/// its own message).
[[nodiscard]] std::vector<Update> PlanUpdates(const simulation::State& state, tick::Tick tick,
                                              std::span<const Recipient> recipients);

/// One round fired, as every client in the match is told of it (ADR-0044).
struct Shot {
  /// The body of the player who fired it.
  simulation::EntityId shooter{};
  /// The server tick it was fired on.
  tick::Tick tick = 0;
  /// Where the round left from.
  math::Vec3 origin{};
  /// Where it left for, as a view's yaw and pitch, in radians.
  float yaw = 0.0F;
  float pitch = 0.0F;
};

/// The Shots of state, the state of tick, in its order. Every recipient is
/// sent every one of them, the shooter included, so there is one per round and
/// not one per recipient.
[[nodiscard]] std::vector<Shot> PlanShots(const simulation::State& state, tick::Tick tick);

/// A hit on a player, as the one client who fired the round is told of it
/// (CONTEXT.md's Hit confirmation, ADR-0044).
struct HitConfirmation {
  /// The entity the recipient's player controls: the shooter's.
  simulation::EntityId recipient{};
  /// The body that was hit.
  simulation::EntityId target{};
  /// The damage the hit did.
  float damage = 0.0F;
  /// Where on the body it struck.
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
};

/// The Hit confirmations of state, one per hit, in its order: each for the
/// shooter alone, so neither the target nor anyone else is told, and what
/// health the target has left is told to no one.
[[nodiscard]] std::vector<HitConfirmation> PlanHitConfirmations(const simulation::State& state);

/// A player's death, as every client in the match is told of it (US-13).
struct Death {
  /// The body of the player who died.
  simulation::EntityId victim{};
  /// The body of the player who fired the killing round.
  simulation::EntityId killer{};
  /// Where the killing round was fired for, as a view's yaw and pitch, in radians.
  float yaw = 0.0F;
  float pitch = 0.0F;
  /// Where on the victim it struck.
  ballistics::BodyPart part = ballistics::BodyPart::kTorso;
};

/// The Deaths of state, in its order. Every recipient is sent every one of
/// them, the victim included, so there is one per death and not one per recipient.
[[nodiscard]] std::vector<Death> PlanDeaths(const simulation::State& state);

}  // namespace augusta::replication

#endif  // AUGUSTA_REPLICATION_H_
