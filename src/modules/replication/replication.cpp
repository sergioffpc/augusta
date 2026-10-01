#include "augusta/replication.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "augusta/simulation.h"
#include "augusta/weapon.h"

namespace augusta::replication {

namespace {

// The rifle of the player who controls entity in state; one with no round if
// state holds no such body.
weapon::State RifleOf(const simulation::State& state, simulation::EntityId entity) {
  const auto body = std::ranges::find(state.bodies, entity, &simulation::EntityState::entity);
  return body == state.bodies.end() ? weapon::State{} : body->rifle;
}

// The health of the player who controls entity in state; 0 if state holds no
// such body, as it holds none of a dead player.
float HealthOf(const simulation::State& state, simulation::EntityId entity) {
  const auto body = std::ranges::find(state.bodies, entity, &simulation::EntityState::entity);
  return body == state.bodies.end() ? 0.0F : body->health;
}

}  // namespace

std::vector<Update> PlanUpdates(const simulation::State& state, std::uint64_t tick,
                                std::span<const Recipient> recipients) {
  std::vector<EntityBody> everyone;
  everyone.reserve(state.bodies.size());
  for (const simulation::EntityState& body : state.bodies) {
    everyone.push_back(EntityBody{.entity = body.entity, .body = body.body, .yaw = body.yaw});
  }

  std::vector<Update> updates;
  updates.reserve(recipients.size());
  for (const Recipient& recipient : recipients) {
    updates.push_back(Update{
        .recipient = recipient.entity,
        .tick = tick,
        .acknowledged_sequence = recipient.acknowledged_sequence,
        .bodies = everyone,
        .rifle = RifleOf(state, recipient.entity),
        .health = HealthOf(state, recipient.entity),
        .queued_commands = recipient.queued_commands,
    });
  }
  return updates;
}

std::vector<Shot> PlanShots(const simulation::State& state, std::uint64_t tick) {
  std::vector<Shot> shots;
  shots.reserve(state.shots.size());
  for (const simulation::Shot& shot : state.shots) {
    shots.push_back(
        Shot{.shooter = shot.shooter, .tick = tick, .origin = shot.origin, .yaw = shot.yaw, .pitch = shot.pitch});
  }
  return shots;
}

std::vector<HitConfirmation> PlanHitConfirmations(const simulation::State& state) {
  std::vector<HitConfirmation> confirmations;
  confirmations.reserve(state.hits.size());
  for (const simulation::Hit& hit : state.hits) {
    confirmations.push_back(
        HitConfirmation{.recipient = hit.shooter, .target = hit.target, .damage = hit.damage, .part = hit.part});
  }
  return confirmations;
}

std::vector<Death> PlanDeaths(const simulation::State& state) {
  std::vector<Death> deaths;
  deaths.reserve(state.deaths.size());
  for (const simulation::Death& death : state.deaths) {
    deaths.push_back(Death{
        .victim = death.victim, .killer = death.killer, .yaw = death.yaw, .pitch = death.pitch, .part = death.part});
  }
  return deaths;
}

}  // namespace augusta::replication
