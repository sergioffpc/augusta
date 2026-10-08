#include "tick_messages.h"

#include <expected>

#include "augusta/failure.h"
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

std::expected<void, failure::Failure> ForEachTickMessage(const simulation::State& state, tick::Tick tick,
                                                         const TickRecipients& to, const TickMessageSink& send) {
  const replication::Updates updates = replication::PlanUpdates(state, tick, to.recipients);
  // One message, its bodies converted once, addressed to each recipient in
  // turn: only the recipient's own fields change between their payloads. It is
  // held as the MessageWire Encode takes, so encoding it copies no body.
  protocol::MessageWire message = ToWire(updates);
  auto& addressed = std::get<protocol::AuthoritativeStateWire>(message);
  for (const replication::RecipientUpdate& recipient : updates.recipients) {
    Address(addressed, recipient);
    const auto payload = EncodeToSend(message);
    if (!payload.has_value()) {
      return std::unexpected(payload.error());
    }
    send(to.peers.at(FromSimulation(recipient.entity)), *payload, networking::Reliability::kUnreliable);
  }
  for (const replication::Shot& shot : replication::PlanShots(state, tick)) {
    const auto payload = EncodeToSend(ToWire(shot));
    if (!payload.has_value()) {
      return std::unexpected(payload.error());
    }
    for (const auto& [entity, peer] : to.peers) {
      send(peer, *payload, networking::Reliability::kReliable);
    }
  }
  for (const replication::HitConfirmation& hit : replication::PlanHitConfirmations(state)) {
    if (const auto shooter = to.peers.find(FromSimulation(hit.recipient)); shooter != to.peers.end()) {
      const auto payload = EncodeToSend(ToWire(hit));
      if (!payload.has_value()) {
        return std::unexpected(payload.error());
      }
      send(shooter->second, *payload, networking::Reliability::kReliable);
    }
  }
  for (const replication::Death& death : replication::PlanDeaths(state)) {
    const auto payload = EncodeToSend(ToWire(death));
    if (!payload.has_value()) {
      return std::unexpected(payload.error());
    }
    for (const auto& [entity, peer] : to.peers) {
      send(peer, *payload, networking::Reliability::kReliable);
    }
  }
  return {};
}

std::expected<void, failure::Failure> SendTickMessages(networking::Server& network, HostMetrics& metrics,
                                                       const simulation::State& state, tick::Tick tick,
                                                       const TickRecipients& to) {
  return ForEachTickMessage(state, tick, to,
                            [&network, &metrics](networking::PeerId peer, const networking::Payload& payload,
                                                 networking::Reliability reliability) {
                              if (TypeOf(payload) == MessageType::kAuthoritativeState) {
                                metrics.authoritative_state_update_bytes.Observe(static_cast<double>(payload.size()));
                              }
                              SendCounted(network, metrics, peer, payload, reliability);
                            });
}

}  // namespace augusta::server
