#include "augusta/replication.h"

#include <algorithm>
#include <cassert>
#include <span>
#include <vector>

#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/weapon.h"

namespace augusta::replication {

namespace {

// The body of the player who controls entity in state, or nullptr if state
// holds no such body, as it holds none of a dead player. state's bodies are
// ordered by EntityId.
const simulation::EntityState* BodyOf(const simulation::State& state, simulation::EntityId entity) {
  const auto body = std::ranges::lower_bound(state.bodies, entity, {}, &simulation::EntityState::entity);
  return body == state.bodies.end() || body->entity != entity ? nullptr : &*body;
}

}  // namespace

Updates PlanUpdates(const simulation::State& state, tick::Tick tick, std::span<const Recipient> recipients) {
  assert(std::ranges::is_sorted(state.bodies, {}, &simulation::EntityState::entity));
  Updates updates{.tick = tick, .bodies = {}, .recipients = {}};
  updates.bodies.reserve(state.bodies.size());
  for (const simulation::EntityState& body : state.bodies) {
    updates.bodies.push_back(EntityBody{.entity = body.entity, .body = body.body, .yaw = body.yaw});
  }
  updates.recipients.reserve(recipients.size());
  for (const Recipient& recipient : recipients) {
    const simulation::EntityState* const body = BodyOf(state, recipient.entity);
    updates.recipients.push_back(RecipientUpdate{
        .recipient = recipient.entity,
        .acknowledged_sequence = recipient.acknowledged_sequence,
        .rifle = body == nullptr ? weapon::State{} : body->rifle,
        .health = body == nullptr ? 0.0F : body->health,
        .queued_commands = recipient.queued_commands,
    });
  }
  return updates;
}

std::vector<Shot> PlanShots(const simulation::State& state, tick::Tick tick) {
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
