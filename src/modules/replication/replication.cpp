#include "augusta/replication.h"

#include <cstddef>
#include <utility>

namespace augusta::replication {

simulation::PlayerId PlayerOf(protocol::SessionId session) {
  return static_cast<simulation::PlayerId>(static_cast<std::uint32_t>(session));
}

protocol::SessionId SessionOf(simulation::PlayerId player) {
  return static_cast<protocol::SessionId>(static_cast<std::uint32_t>(player));
}

std::vector<Update> PlanUpdates(const simulation::State& state, std::uint32_t tick,
                                std::span<const Recipient> recipients) {
  protocol::AuthoritativeState everyone;
  everyone.tick = tick;
  everyone.players.reserve(state.players.size());
  for (const simulation::PlayerState& player : state.players) {
    everyone.players.push_back(protocol::PlayerState{.session = SessionOf(player.player), .body = player.body});
  }

  std::vector<Update> updates;
  updates.reserve(recipients.size());
  for (const Recipient& recipient : recipients) {
    Update update{.recipient = recipient.session, .state = everyone};
    update.state.acknowledged_sequence = recipient.acknowledged_sequence;
    updates.push_back(std::move(update));
  }
  return updates;
}

}  // namespace augusta::replication
