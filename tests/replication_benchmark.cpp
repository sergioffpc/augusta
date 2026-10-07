#include <cstddef>
#include <cstdint>

#include <benchmark/benchmark.h>

#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "match.h"
#include "tick_messages.h"

// Replicating one tick's Authoritative State (ADR-0038): every player in a full
// match is sent every body, with its own acknowledgement, rifle, health and
// queued commands, each encoded as the payload the Host hands its connection.
namespace {

using augusta::simulation::EntityId;

constexpr std::size_t kPlayers = augusta::protocol::kMaxPlayers;

augusta::simulation::State FullMatchState() {
  augusta::simulation::State state;
  state.tick = 123'456;
  for (std::size_t player = 0; player < kPlayers; ++player) {
    augusta::simulation::EntityState body{.entity = static_cast<EntityId>(player + 1), .yaw = 1.5F, .health = 66.0F};
    body.body.position = augusta::math::Vec3(static_cast<float>(player) * 3.0F, 0.0F, -12.5F);
    body.body.velocity = augusta::math::Vec3(1.25F, 0.0F, -4.5F);
    body.body.stamina = 0.75F;
    body.rifle.rounds = 17;
    state.bodies.push_back(body);
  }
  return state;
}

augusta::server::TickRecipients EveryPlayer() {
  augusta::server::TickRecipients to;
  for (std::size_t player = 0; player < kPlayers; ++player) {
    const auto number = static_cast<std::uint32_t>(player + 1);
    to.recipients.push_back(augusta::replication::Recipient{
        .entity = static_cast<EntityId>(number), .acknowledged_sequence = 4'321, .queued_commands = 2});
    to.peers.emplace(static_cast<augusta::server::EntityId>(number), static_cast<augusta::networking::PeerId>(number));
  }
  return to;
}

void BM_ReplicateAuthoritativeState(benchmark::State& state) {
  const augusta::simulation::State tick_state = FullMatchState();
  const augusta::server::TickRecipients to = EveryPlayer();
  for (auto _ : state) {
    augusta::server::ForEachTickMessage(
        tick_state, tick_state.tick, to,
        [](augusta::networking::PeerId peer, const augusta::networking::Payload& payload,
           augusta::networking::Reliability reliability) {
          benchmark::DoNotOptimize(peer);
          benchmark::DoNotOptimize(payload.data());
          benchmark::DoNotOptimize(reliability);
        });
  }
}
BENCHMARK(BM_ReplicateAuthoritativeState);

}  // namespace
