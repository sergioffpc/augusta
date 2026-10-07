#include "tick_messages.h"

#include <variant>

#include "augusta/networking.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "host_metrics.h"
#include "simulation_mapping.h"
#include "wire.h"

namespace augusta::server {

void SendCounted(networking::Server& network, HostMetrics& metrics, networking::PeerId peer,
                 const networking::Payload& payload, networking::Reliability reliability) {
  CountSent(metrics, payload);
  network.Send(peer, payload, reliability);
}

void ForEachTickMessage(const simulation::State& state, tick::Tick tick, const TickRecipients& to,
                        const TickMessageSink& send) {
  const replication::Updates updates = replication::PlanUpdates(state, tick, to.recipients);
  // One message, its bodies converted once, addressed to each recipient in
  // turn: only the recipient's own fields change between their payloads.
  protocol::MessageWire message = ToWire(updates);
  auto& addressed = std::get<protocol::AuthoritativeStateWire>(message);
  for (const replication::RecipientUpdate& recipient : updates.recipients) {
    Address(addressed, recipient);
    send(to.peers.at(FromSimulation(recipient.recipient)), protocol::Encode(message),
         networking::Reliability::kUnreliable);
  }
  for (const replication::Shot& shot : replication::PlanShots(state, tick)) {
    const protocol::BytesWire payload = protocol::Encode(ToWire(shot));
    for (const auto& [entity, peer] : to.peers) {
      send(peer, payload, networking::Reliability::kReliable);
    }
  }
  for (const replication::HitConfirmation& hit : replication::PlanHitConfirmations(state)) {
    if (const auto shooter = to.peers.find(FromSimulation(hit.recipient)); shooter != to.peers.end()) {
      send(shooter->second, protocol::Encode(ToWire(hit)), networking::Reliability::kReliable);
    }
  }
  for (const replication::Death& death : replication::PlanDeaths(state)) {
    const protocol::BytesWire payload = protocol::Encode(ToWire(death));
    for (const auto& [entity, peer] : to.peers) {
      send(peer, payload, networking::Reliability::kReliable);
    }
  }
}

void SendTickMessages(networking::Server& network, HostMetrics& metrics, const simulation::State& state,
                      tick::Tick tick, const TickRecipients& to) {
  ForEachTickMessage(state, tick, to,
                     [&network, &metrics](networking::PeerId peer, const networking::Payload& payload,
                                          networking::Reliability reliability) {
                       if (TypeOf(payload) == MessageType::kAuthoritativeState) {
                         metrics.authoritative_state_update_bytes.Observe(static_cast<double>(payload.size()));
                       }
                       SendCounted(network, metrics, peer, payload, reliability);
                     });
}

}  // namespace augusta::server
