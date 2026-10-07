#ifndef AUGUSTA_SERVER_TICK_MESSAGES_H_
#define AUGUSTA_SERVER_TICK_MESSAGES_H_

#include <unordered_map>
#include <vector>

#include "augusta/networking.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "host_metrics.h"
#include "match.h"

/// \file
/// What Host sends its players after each tick (ADR-0044): each one's update,
/// unreliably, since a newer one supersedes it; then, reliably, what must
/// arrive: every Shot and every Death of the tick to all of them, and each Hit
/// confirmation to its shooter alone, if it is still in the match. replication
/// decides what each message holds; this sends them on the Host's connections,
/// counting each into the Host's metrics (host_metrics.h).
namespace augusta::server {

/// Who a tick's messages go to: each player in the match.
struct TickRecipients {
  std::vector<replication::Recipient> recipients;
  /// The connection of each recipient, by the entity its player controls.
  std::unordered_map<EntityId, networking::PeerId> peers;
};

/// Sends payload, an encoded message, to peer as reliability says, counting it
/// into metrics: how the Host sends everything it sends.
void SendCounted(networking::Server& network, HostMetrics& metrics, networking::PeerId peer,
                 const networking::Payload& payload, networking::Reliability reliability);

/// Sends to the recipients what tick's state holds for them, counting it into metrics.
void SendTickMessages(networking::Server& network, HostMetrics& metrics, const simulation::State& state,
                      tick::Tick tick, const TickRecipients& to);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_TICK_MESSAGES_H_
