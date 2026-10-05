#include "tick_messages.h"

#include "augusta/networking.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "simulation_mapping.h"
#include "wire.h"

namespace augusta::server {

void SendTickMessages(networking::Server& network, const simulation::State& state, tick::Tick tick,
                      const TickRecipients& to) {
  for (const replication::Update& update : replication::PlanUpdates(state, tick, to.recipients)) {
    network.Send(to.peers.at(FromSimulation(update.recipient)), protocol::Encode(ToWire(update)),
                 networking::Reliability::kUnreliable);
  }
  for (const replication::Shot& shot : replication::PlanShots(state, tick)) {
    const protocol::BytesWire payload = protocol::Encode(ToWire(shot));
    for (const auto& [entity, peer] : to.peers) {
      network.Send(peer, payload, networking::Reliability::kReliable);
    }
  }
  for (const replication::HitConfirmation& hit : replication::PlanHitConfirmations(state)) {
    if (const auto shooter = to.peers.find(FromSimulation(hit.recipient)); shooter != to.peers.end()) {
      network.Send(shooter->second, protocol::Encode(ToWire(hit)), networking::Reliability::kReliable);
    }
  }
  for (const replication::Death& death : replication::PlanDeaths(state)) {
    const protocol::BytesWire payload = protocol::Encode(ToWire(death));
    for (const auto& [entity, peer] : to.peers) {
      network.Send(peer, payload, networking::Reliability::kReliable);
    }
  }
}

}  // namespace augusta::server
