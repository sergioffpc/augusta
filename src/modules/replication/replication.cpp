#include "augusta/replication.h"

#include <cstdint>
#include <span>
#include <vector>

#include "augusta/simulation.h"

namespace augusta::replication {

std::vector<Update> PlanUpdates(const simulation::State& state, std::uint32_t tick,
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
        .queued_commands = recipient.queued_commands,
    });
  }
  return updates;
}

std::vector<Shot> PlanShots(const simulation::State& state, std::uint32_t tick) {
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

}  // namespace augusta::replication
