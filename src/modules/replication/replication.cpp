#include "augusta/replication.h"

#include <cstddef>

namespace augusta::replication {

simulation::PlayerId PlayerOf(identity::SessionId session) {
  return static_cast<simulation::PlayerId>(static_cast<std::uint32_t>(session));
}

identity::SessionId SessionOf(simulation::PlayerId player) {
  return static_cast<identity::SessionId>(static_cast<std::uint32_t>(player));
}

std::vector<Update> PlanUpdates(const simulation::State& state, std::uint32_t tick,
                                std::span<const Recipient> recipients) {
  std::vector<PlayerBody> everyone;
  everyone.reserve(state.players.size());
  for (const simulation::PlayerState& player : state.players) {
    everyone.push_back(PlayerBody{.session = SessionOf(player.player), .body = player.body});
  }

  std::vector<Update> updates;
  updates.reserve(recipients.size());
  for (const Recipient& recipient : recipients) {
    updates.push_back(Update{
        .recipient = recipient.session,
        .tick = tick,
        .acknowledged_sequence = recipient.acknowledged_sequence,
        .players = everyone,
    });
  }
  return updates;
}

}  // namespace augusta::replication
