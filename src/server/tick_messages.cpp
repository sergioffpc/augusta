#include "tick_messages.h"

#include "augusta/networking.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "host_metrics.h"
#include "simulation_mapping.h"
#include "wire.h"

namespace augusta::server {

namespace {

// Sends payload to peer as reliability says, counting it into metrics.
void Send(networking::Server& network, HostMetrics& metrics, networking::PeerId peer,
          const networking::Payload& payload, networking::Reliability reliability) {
  CountSent(metrics, payload);
  network.Send(peer, payload, reliability);
}

}  // namespace

void SendTickMessages(networking::Server& network, HostMetrics& metrics, const simulation::State& state,
                      tick::Tick tick, const TickRecipients& to) {
  for (const replication::Update& update : replication::PlanUpdates(state, tick, to.recipients)) {
    const protocol::BytesWire payload = protocol::Encode(ToWire(update));
    metrics.authoritative_state_update_bytes.Observe(static_cast<double>(payload.size()));
    Send(network, metrics, to.peers.at(FromSimulation(update.recipient)), payload,
         networking::Reliability::kUnreliable);
  }
  for (const replication::Shot& shot : replication::PlanShots(state, tick)) {
    const protocol::BytesWire payload = protocol::Encode(ToWire(shot));
    for (const auto& [entity, peer] : to.peers) {
      Send(network, metrics, peer, payload, networking::Reliability::kReliable);
    }
  }
  for (const replication::HitConfirmation& hit : replication::PlanHitConfirmations(state)) {
    if (const auto shooter = to.peers.find(FromSimulation(hit.recipient)); shooter != to.peers.end()) {
      Send(network, metrics, shooter->second, protocol::Encode(ToWire(hit)), networking::Reliability::kReliable);
    }
  }
  for (const replication::Death& death : replication::PlanDeaths(state)) {
    const protocol::BytesWire payload = protocol::Encode(ToWire(death));
    for (const auto& [entity, peer] : to.peers) {
      Send(network, metrics, peer, payload, networking::Reliability::kReliable);
    }
  }
}

}  // namespace augusta::server
