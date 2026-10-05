#ifndef AUGUSTA_SERVER_TICK_MESSAGES_H_
#define AUGUSTA_SERVER_TICK_MESSAGES_H_

#include <unordered_map>
#include <vector>

#include "augusta/networking.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "match.h"

/// \file
/// What Host sends its players after each tick (ADR-0044): each one's update,
/// unreliably, since a newer one supersedes it; then, reliably, what must
/// arrive: every Shot and every Death of the tick to all of them, and each Hit
/// confirmation to its shooter alone, if it is still in the match. replication
/// decides what each message holds; this sends them on the Host's connections.
namespace augusta::server {

/// Who a tick's messages go to: each player in the match.
struct TickRecipients {
  std::vector<replication::Recipient> recipients;
  /// The connection of each recipient, by the entity its player controls.
  std::unordered_map<EntityId, networking::PeerId> peers;
};

/// Sends to the recipients what tick's state holds for them.
void SendTickMessages(networking::Server& network, const simulation::State& state, tick::Tick tick,
                      const TickRecipients& to);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_TICK_MESSAGES_H_
