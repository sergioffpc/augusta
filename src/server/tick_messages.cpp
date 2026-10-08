#include "tick_messages.h"

#include <expected>
#include <optional>
#include <utility>

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

networking::SendResult SendCounted(networking::Server& network, HostMetrics& metrics, networking::PeerId peer,
                                   const networking::Payload& payload, networking::Reliability reliability) {
  networking::SendResult sent = network.Send(peer, payload, reliability);
  if (sent == networking::SendOutcome::kAccepted) {
    CountSent(metrics, payload);
  }
  return sent;
}

void ForEachTickMessage(const simulation::State& state, tick::Tick tick, const TickRecipients& to,
                        const TickMessageSink& send) {
  const replication::Updates updates = replication::PlanUpdates(state, tick, to.recipients);
  // One message, its bodies converted once, addressed to each recipient in
  // turn: only the recipient's own fields change between their payloads. It is
  // held as the MessageWire Encode takes, so encoding it copies no body.
  protocol::MessageWire message = ToWire(updates);
  auto& addressed = std::get<protocol::AuthoritativeStateWire>(message);
  for (const replication::RecipientUpdate& recipient : updates.recipients) {
    Address(addressed, recipient);
    send(to.peers.at(FromSimulation(recipient.entity)), protocol::Encode(message),
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

std::expected<void, failure::Failure> SendTickMessages(networking::Server& network, HostMetrics& metrics,
                                                       const simulation::State& state, tick::Tick tick,
                                                       const TickRecipients& to) {
  std::optional<failure::Failure> failed;
  ForEachTickMessage(
      state, tick, to,
      [&network, &metrics, &failed](networking::PeerId peer, const networking::Payload& payload,
                                    networking::Reliability reliability) {
        // Once the transport has failed, nothing more is sent on it.
        if (failed.has_value()) {
          return;
        }
        networking::SendResult sent = SendCounted(network, metrics, peer, payload, reliability);
        if (!sent.has_value()) {
          failed = std::move(sent.error());
        } else if (*sent == networking::SendOutcome::kAccepted && TypeOf(payload) == MessageType::kAuthoritativeState) {
          metrics.authoritative_state_update_bytes.Observe(static_cast<double>(payload.size()));
        }
      });
  if (failed.has_value()) {
    return std::unexpected(std::move(*failed));
  }
  return {};
}

}  // namespace augusta::server
