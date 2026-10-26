#ifndef AUGUSTA_SIMULATION_COMBAT_H_
#define AUGUSTA_SIMULATION_COMBAT_H_

#include <cstdint>

#include "augusta/body_part.h"
#include "augusta/command.h"
#include "augusta/tick.h"

/// \file
/// The decisions SimulationWorld's phases take about a round: the Shooter's
/// delay it is judged with (ADR-0044) and the damage a hit of it deals. Pure -
/// no ECS, no physics, no clock - and private to the simulation module; the
/// systems that act on them are World::Impl's. HitboxHistoryTicks, the public
/// one of them, is declared in simulation.h.
namespace augusta::simulation {

/// kMaxShootersDelay in ticks at tick_rate_hz: not a whole number at every rate.
[[nodiscard]] float MaxShootersDelayTicks(std::uint8_t tick_rate_hz);

/// The Shooter's delay (ADR-0044), in ticks, of a round fired on tick by
/// command: from the Seen time command reports to tick, and no more than
/// max_delay. The Seen time is only what a client says, so its fraction is held
/// within 0 to 1 and the whole of it to no newer than the last tick's State, the
/// newest any client has been sent.
[[nodiscard]] float ShootersDelay(tick::Tick tick, const command::Command& command, float max_delay);

/// What damage gives for a hit on part.
[[nodiscard]] float DamageFor(const body_part::Damage& damage, body_part::BodyPart part);

}  // namespace augusta::simulation

#endif  // AUGUSTA_SIMULATION_COMBAT_H_
