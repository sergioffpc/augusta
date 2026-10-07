#ifndef AUGUSTA_WEAPON_H_
#define AUGUSTA_WEAPON_H_

#include <cstdint>

#include "augusta/command.h"
#include "augusta/parameters.h"

/// \file
/// augusta::weapon holds the rifle's rules (US-07 to US-09): what a tick's
/// Command does to a player's rifle, whether it fires a round, and where that
/// round leaves for. It is ARCHITECTURE.md §5's WeaponHandling: pure mechanism
/// in the shared core, with no world and no state of its own, so the two Worlds
/// that run a WeaponHandling phase call the same function, as both call
/// physics::World. The server's SimulationWorld takes the result as
/// authoritative and fires a bullet for each round (ADR-0023); the client's
/// PredictionWorld predicts its own fire, reload and recoil with it (ADR-0024),
/// and is reconciled against the server's State (ADR-0004). Nothing here is
/// random, so the two agree.
///
/// The rifle's values are the Parameters' (ADR-0039), handed in on every call.
namespace augusta::weapon {

/// How far a rifle points off its player's view (CONTEXT.md's Recoil offset),
/// in radians: a positive pitch up, a positive yaw left, as command::Command's
/// view turns. Each on the grid an angle travels on (math::kAngleGrid).
struct RecoilOffset {
  float pitch = 0.0F;
  float yaw = 0.0F;

  bool operator==(const RecoilOffset&) const = default;
};

/// One player's rifle between two ticks.
struct State {
  /// How long, in seconds, until the next round may fire; 0 or less when one
  /// may. Below 0 only while the trigger is held: see Step.
  float cooldown = 0.0F;
  /// How long, in seconds, the reload under way still takes; 0 when there is none.
  float reload_remaining = 0.0F;
  /// What the rounds fired so far have left the rifle pointing off the view by.
  RecoilOffset recoil{};
  /// How many rounds are left in the magazine.
  std::uint8_t rounds = 0;
  /// How many rounds the Burst under way has fired: which kick of the recoil
  /// pattern the next one gives. 0 when there is none.
  std::uint8_t burst_index = 0;

  bool operator==(const State&) const = default;
};

/// rifle with a full magazine, ready to fire along the view: what a player
/// starts a Match with.
[[nodiscard]] State Loaded(const parameters::Rifle& rifle);

/// What one tick did to a rifle.
struct Result {
  /// The rifle after the tick.
  State state{};
  /// Where the round fired left for, as a view's yaw and pitch in radians
  /// (command::ViewDirection gives the direction): the Command's view turned by
  /// the Recoil offset the rifle had. Only of a tick that fired.
  float yaw = 0.0F;
  float pitch = 0.0F;
  /// Whether a round was fired on it. At most one fires a tick.
  bool fired = false;
};

/// Runs one fixed tick of delta_time seconds of rifle, in state, on command.
///
/// Fire (US-07): while command holds fire and the magazine has a round, a round
/// fires on the first tick one may, and then one every fire interval (60 /
/// rounds_per_minute seconds): a tap fires one round, and holding fires at the
/// rifle's rate until the magazine is empty. An interval that is not a whole
/// number of ticks is kept on average, the fraction of a tick a round waited
/// being taken off the wait for the next; a released trigger carries nothing
/// over. An interval shorter than a tick fires every tick.
///
/// Reload (US-08): a command that presses reload with a magazine that is not
/// full starts a reload of reload_seconds, counted from that tick and rounded
/// up to whole ticks. No round fires on a tick of it, the one it starts on
/// included, and with its last tick the magazine is full. A press with a full
/// magazine, or during a reload, does nothing: a reload is never started over.
///
/// Recoil (US-09): a round leaves along the view turned by the Recoil offset,
/// and then adds its kick to it: the recoil pattern's kick for its place in the
/// Burst, the last kick again past the pattern's end, scaled by
/// ads_recoil_scale while command holds ads. So the first round of a rifle at
/// rest leaves along the view, and each one after it further off. A Burst lasts
/// while the trigger is pulled: fire held, with a round in the magazine and no
/// reload under way. On a tick it is not, the next round is a Burst's first
/// again, and the offset shrinks toward zero by recoil_recovery_per_second. The
/// offset is the rifle's alone: command's yaw and pitch are never changed.
[[nodiscard]] Result Step(const parameters::Rifle& rifle, const State& state, const command::Command& command,
                          float delta_time);

}  // namespace augusta::weapon

#endif  // AUGUSTA_WEAPON_H_
