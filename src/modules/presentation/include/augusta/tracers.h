#ifndef AUGUSTA_TRACERS_H_
#define AUGUSTA_TRACERS_H_

#include <span>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/effects.h"
#include "augusta/math.h"
#include "augusta/physics.h"

// Tracers (ADR-0024, ADR-0044): every Shot the server announces is drawn as a
// tracer along the trajectory the server computes for it - the same
// ballistics::World, with the Parameters' muzzle velocity and ammo, stepped a
// server tick at a time - until it meets the Map, where it leaves an impact,
// or reaches its max range. A tracer is a visual only: it is handed no
// player's hitboxes, so it never hits a player (ADR-0044: hits on players are
// drawn only from the server's Hit confirmations).
//
// Pure of clocks and ECS - the caller hands in each frame's elapsed time - so
// it is tested on its own; PresentationWorld feeds it (see presentation.cpp).
namespace augusta::presentation {

/// How a Shot's bullet flies: the Parameters' muzzle velocity and ammo, and the
/// server's tick, which the trajectory is stepped by as the server steps it.
struct TracerRules {
  float muzzle_velocity = 1.0F;
  ballistics::BulletConfig bullet{};
  /// The server's tick duration, in seconds.
  float tick_duration = 1.0F;
};

/// One tracer as drawn this frame: a streak along the trajectory from tail,
/// where the bullet was a tick ago, to head, where it is now - both between
/// the two ticks around them, the fraction of a tick elapsed of the way.
struct Tracer {
  math::Vec3 head{};
  math::Vec3 tail{};
};

/// Every tracer in flight and the impacts they left on the Map.
class Tracers {
 public:
  /// map is the Map tracers meet: only its collision (physics::World::RaycastMap),
  /// never a body. It must outlive this Tracers.
  explicit Tracers(const physics::World& map);

  /// Starts a tracer at origin, a Shot's, flying along direction under rules.
  void Fire(const math::Vec3& origin, const math::Vec3& direction, const TracerRules& rules);

  /// Moves every tracer on by elapsed seconds, stepping its trajectory by each
  /// tick that completes, and ages every impact. A tracer whose bullet reaches
  /// the Map ends there with an impact, aged by the time since; one past its
  /// max range just ends.
  void Advance(float elapsed);

  /// Every tracer in flight, as drawn now.
  [[nodiscard]] std::vector<Tracer> Drawn() const;

  /// Every impact on the Map still showing (kImpactSeconds).
  [[nodiscard]] std::span<const Effect> Impacts() const;

 private:
  struct Flight {
    ballistics::BulletHandle bullet{};
    float tick_duration = 1.0F;
    // Where the bullet was a tick before the tick it is flying from, and the
    // tick it is flying from and to: the trajectory's last three points.
    math::Vec3 behind{};
    math::Vec3 from{};
    math::Vec3 to{};
    // Whether the bullet's flight ends at to, and whether on the Map.
    bool ends = false;
    bool strikes_map = false;
    // How far, in seconds, the bullet is from from toward to.
    float elapsed = 0.0F;
  };

  // Steps flight's bullet one tick on from to.
  void StepAhead(Flight& flight);
  // Moves flight on by elapsed seconds; false once it has ended.
  bool AdvanceFlight(Flight& flight, float elapsed);

  const physics::World& map_;
  ballistics::World ballistics_;
  std::vector<Flight> flights_;
  std::vector<Effect> impacts_;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_TRACERS_H_
