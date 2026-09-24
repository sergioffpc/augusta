#include "augusta/replication.h"

#include <cstddef>

namespace augusta::replication {

std::vector<Update> PlanUpdates(const simulation::State& state, std::uint32_t tick,
                                std::span<const Recipient> recipients) {
  std::vector<PlayerBody> everyone;
  everyone.reserve(state.players.size());
  for (const simulation::PlayerState& player : state.players) {
    everyone.push_back(PlayerBody{.player = player.player, .body = player.body});
  }

  std::vector<Update> updates;
  updates.reserve(recipients.size());
  for (const Recipient& recipient : recipients) {
    updates.push_back(Update{
        .recipient = recipient.player,
        .tick = tick,
        .acknowledged_sequence = recipient.acknowledged_sequence,
        .players = everyone,
    });
  }
  return updates;
}

}  // namespace augusta::replication
