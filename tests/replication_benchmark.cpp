#include <variant>
#include <vector>

#include <benchmark/benchmark.h>

#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "benchmark_match.h"
#include "wire.h"

// A full Match's Authoritative State replicated as server::Host replicates it
// after each tick (ADR-0044): what each player in the Match is sent
// (replication::PlanUpdates), in the protocol's terms and encoded, one message
// per recipient: everything Host does to send its Authoritative State updates
// but the send itself. Its Shots, Hit confirmations and Deaths are one message
// each, not one per recipient, and are not timed here.
namespace {

using augusta::replication::Recipient;
using augusta::replication::RecipientUpdate;

void BM_Replication(benchmark::State& state) {
  const auto match = augusta::benchmarks::StartFullMatch(state);
  if (!match.has_value()) {
    return;
  }
  const augusta::simulation::State& resolved = match->last_state;
  // Every player in the Match, each with a command acknowledged and a couple
  // still queued: any values do, each recipient's are encoded the same way.
  std::vector<Recipient> recipients;
  for (const auto& body : resolved.bodies) {
    recipients.push_back(Recipient{.entity = body.entity, .acknowledged_sequence = 4'321, .queued_commands = 2});
  }
  for (auto _ : state) {
    const auto updates = augusta::replication::PlanUpdates(resolved, resolved.tick, recipients);
    augusta::protocol::MessageWire message = augusta::server::ToWire(updates);
    auto& addressed = std::get<augusta::protocol::AuthoritativeStateWire>(message);
    for (const RecipientUpdate& recipient : updates.recipients) {
      augusta::server::Address(addressed, recipient);
      benchmark::DoNotOptimize(augusta::protocol::Encode(message));
    }
  }
}
BENCHMARK(BM_Replication);

}  // namespace
