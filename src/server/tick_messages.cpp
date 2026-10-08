#include "tick_messages.h"

#include <expected>
#include <optional>
#include <utility>
#include <vector>

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

namespace {

// One message of a tick, encoded, and who it goes to.
struct Addressed {
  std::vector<networking::PeerId> peers;
  networking::Payload payload;
  networking::Reliability reliability{};
};

}  // namespace

std::expected<void, failure::Failure> ForEachTickMessage(const simulation::State& state, tick::Tick tick,
                                                         const TickRecipients& to, const TickMessageSink& send) {
  // Every message is encoded before any is handed on, so one the protocol
  // cannot carry stops the whole tick, not the rest of it.
  std::vector<Addressed> tick_messages;
  std::optional<failure::Failure> broken;
  const auto add = [&tick_messages, &broken](const protocol::MessageWire& message,
                                             std::vector<networking::PeerId> peers,
                                             networking::Reliability reliability) {
    if (broken.has_value()) {
      return;
    }
    auto payload = EncodeToSend(message);
    if (!payload.has_value()) {
      broken = std::move(payload.error());
      return;
    }
    tick_messages.push_back({.peers = std::move(peers), .payload = *std::move(payload), .reliability = reliability});
  };
  std::vector<networking::PeerId> everyone;
  everyone.reserve(to.peers.size());
  for (const auto& [entity, peer] : to.peers) {
    everyone.push_back(peer);
  }

  const replication::Updates updates = replication::PlanUpdates(state, tick, to.recipients);
  // One message, its bodies converted once, addressed to each recipient in
  // turn: only the recipient's own fields change between their payloads. It is
  // held as the MessageWire Encode takes, so encoding it copies no body.
  protocol::MessageWire message = ToWire(updates);
  auto& addressed = std::get<protocol::AuthoritativeStateWire>(message);
  for (const replication::RecipientUpdate& recipient : updates.recipients) {
    Address(addressed, recipient);
    add(message, {to.peers.at(FromSimulation(recipient.entity))}, networking::Reliability::kUnreliable);
  }
  for (const replication::Shot& shot : replication::PlanShots(state, tick)) {
    add(ToWire(shot), everyone, networking::Reliability::kReliable);
  }
  for (const replication::HitConfirmation& hit : replication::PlanHitConfirmations(state)) {
    if (const auto shooter = to.peers.find(FromSimulation(hit.recipient)); shooter != to.peers.end()) {
      add(ToWire(hit), {shooter->second}, networking::Reliability::kReliable);
    }
  }
  for (const replication::Death& death : replication::PlanDeaths(state)) {
    add(ToWire(death), everyone, networking::Reliability::kReliable);
  }
  if (broken.has_value()) {
    return std::unexpected(*std::move(broken));
  }

  for (const Addressed& outgoing : tick_messages) {
    for (const networking::PeerId peer : outgoing.peers) {
      send(peer, outgoing.payload, outgoing.reliability);
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
