#ifndef AUGUSTA_WEAPON_H_
#define AUGUSTA_WEAPON_H_

#include <cstdint>

#include "augusta/command.h"
#include "augusta/parameters.h"

// augusta::weapon holds the rifle's rules (US-07): what a tick's Command does
// to a player's rifle, and whether it fires a round. It is ARCHITECTURE.md
// §5's WeaponHandling: pure mechanism in the shared core, with no world and no
// state of its own, so the two Worlds that run a WeaponHandling phase call the
// same function, as both call physics::World. The server's SimulationWorld
// takes the result as authoritative and fires a bullet for each round
// (ADR-0023); the client's PredictionWorld is to predict its own fire with it
// (ADR-0024). Nothing here is random, so the two agree.
//
// The rifle's values are the Parameters' (ADR-0039), handed in on every call.
namespace augusta::weapon {

/// One player's rifle between two ticks.
struct State {
  /// How long, in seconds, until the next round may fire; 0 or less when one
  /// may. Below 0 only while the trigger is held: see Step.
  float cooldown = 0.0F;
  /// How many rounds are left in the magazine.
  std::uint8_t rounds = 0;

  bool operator==(const State&) const = default;
};

/// rifle with a full magazine, ready to fire: what a player starts a Match with.
[[nodiscard]] State Loaded(const parameters::Rifle& rifle);

/// What one tick did to a rifle.
struct Result {
  /// The rifle after the tick.
  State state{};
  /// Whether a round was fired on it. At most one fires a tick.
  bool fired = false;
};

/// Runs one fixed tick of delta_time seconds of rifle, in state, on command.
/// While command holds fire and the magazine has a round, a round fires on the
/// first tick one may, and then one every fire interval (60 / rounds_per_minute
/// seconds): a tap fires one round, and holding fires at the rifle's rate until
/// the magazine is empty. An interval that is not a whole number of ticks is
/// kept on average, the fraction of a tick a round waited being taken off the
/// wait for the next; a released trigger carries nothing over. An interval
/// shorter than a tick fires every tick.
[[nodiscard]] Result Step(const parameters::Rifle& rifle, const State& state, const command::Command& command,
                          float delta_time);

}  // namespace augusta::weapon

#endif  // AUGUSTA_WEAPON_H_
