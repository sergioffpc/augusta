#include "augusta/tracers.h"

#include <span>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/effects.h"
#include "augusta/math.h"
#include "augusta/physics.h"

namespace augusta::presentation {

Tracers::Tracers(const physics::World& map) : map_(map) {}

void Tracers::Fire(const math::Vec3& origin, const math::Vec3& direction, const TracerRules& rules) {
  Flight flight{
      .bullet = ballistics_.Fire(origin, direction, rules.muzzle_velocity, rules.bullet),
      .tick_duration = rules.tick_duration,
      .behind = origin,
      .from = origin,
      .to = origin,
  };
  // A tick ahead of what is drawn, so a frame between two ticks has both ends.
  StepAhead(flight);
  flights_.push_back(flight);
}

void Tracers::Advance(float elapsed) {
  Age(impacts_, elapsed, kImpactSeconds);
  std::erase_if(flights_, [this, elapsed](Flight& flight) { return !AdvanceFlight(flight, elapsed); });
}

std::vector<Tracer> Tracers::Drawn() const {
  std::vector<Tracer> drawn;
  drawn.reserve(flights_.size());
  for (const Flight& flight : flights_) {
    const float fraction = flight.elapsed / flight.tick_duration;
    drawn.push_back(Tracer{.head = math::Lerp(flight.from, flight.to, fraction),
                           .tail = math::Lerp(flight.behind, flight.from, fraction)});
  }
  return drawn;
}

std::span<const Effect> Tracers::Impacts() const { return impacts_; }

void Tracers::StepAhead(Flight& flight) {
  // No hitboxes: a tracer never hits a player (see tracers.h).
  const ballistics::StepResult step = ballistics_.Step(flight.bullet, flight.tick_duration, map_, {});
  flight.behind = flight.from;
  flight.from = flight.to;
  switch (step.outcome) {
    case ballistics::Outcome::kInFlight:
    case ballistics::Outcome::kExpired:
      flight.to = step.state.position;
      break;
    case ballistics::Outcome::kHitMap:
    case ballistics::Outcome::kHitPlayer:
      flight.to = step.impact_point;
      flight.strikes_map = step.outcome == ballistics::Outcome::kHitMap;
      break;
  }
  flight.ends = step.outcome != ballistics::Outcome::kInFlight;
}

bool Tracers::AdvanceFlight(Flight& flight, float elapsed) {
  flight.elapsed += elapsed;
  while (flight.elapsed >= flight.tick_duration) {
    flight.elapsed -= flight.tick_duration;
    if (flight.ends) {
      if (flight.strikes_map) {
        impacts_.push_back(Effect{.position = flight.to, .age = flight.elapsed});
      }
      return false;
    }
    StepAhead(flight);
  }
  return true;
}

}  // namespace augusta::presentation
