#ifndef AUGUSTA_SIMULATION_HITBOX_HISTORY_H_
#define AUGUSTA_SIMULATION_HITBOX_HISTORY_H_

#include <cstddef>
#include <deque>
#include <optional>

#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/tick.h"

/// \file
/// The Hitbox history (CONTEXT.md, ADR-0044): what SimulationWorld keeps of
/// every player's recent ticks, to judge a bullet against the players as its
/// shooter saw them (Lag compensation). Pure - no ECS, no physics, no clock - and
/// private to the simulation module: nothing but SimulationWorld judges a hit.
namespace augusta::simulation {

/// Where a player's body was on one tick, as that tick's Authoritative State
/// reported it: all that places its hitboxes.
struct Pose {
  math::Vec3 position{};
  /// Where it faced, in radians.
  float yaw = 0.0F;
  physics::Stance stance = physics::Stance::kStanding;
};

/// One player's poses of its most recent ticks.
class PoseHistory {
 public:
  /// A history that keeps the poses of the newest capacity ticks.
  explicit PoseHistory(std::size_t capacity = 0) : capacity_(capacity) {}

  /// Keeps pose as that of tick, the tick after the newest kept; beyond the
  /// capacity, the oldest goes.
  void Record(tick::Tick tick, const Pose& pose);

  /// The pose at time, a moment between ticks counted in ticks: position and
  /// yaw interpolated between the two ticks around it, the yaw along the
  /// shorter arc, and the stance of the nearer one, which is how a client shows
  /// a position and a stance between two updates (presentation's
  /// RemoteInterpolator). A time before the oldest tick kept or after the
  /// newest gives that tick's pose. nullopt while none is kept.
  [[nodiscard]] std::optional<Pose> At(double time) const;

 private:
  std::size_t capacity_;
  // Oldest first; the last is newest_tick_'s.
  std::deque<Pose> poses_;
  tick::Tick newest_tick_ = 0;
};

}  // namespace augusta::simulation

#endif  // AUGUSTA_SIMULATION_HITBOX_HISTORY_H_
