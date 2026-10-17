#include <vector>

#include <benchmark/benchmark.h>

#include "augusta/link.h"
#include "augusta/link_mapping.h"
#include "augusta/link_types.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "benchmark_match.h"

// A full Match's Authoritative State replicated as host::Host replicates it
// after each tick (ADR-0044): what each player in the Match is sent
// (replication::PlanUpdates), in the Link's terms, its bodies encoded by the
// Link once and readdressed to each recipient, one message per recipient:
// everything Host does to send its Authoritative State updates but the send
// itself. Its Shots, Hit confirmations and Deaths are one message each, not one
// per recipient, and are not timed here.
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
    augusta::link::AuthoritativeState own = augusta::host::ToLink(updates);
    augusta::host::Address(own, updates.recipients.front());
    const augusta::link::Outgoing shared = augusta::link::Encode(own).value();
    for (const RecipientUpdate& recipient : updates.recipients) {
      augusta::host::Address(own, recipient);
      benchmark::DoNotOptimize(augusta::link::Readdressed(shared, own).value());
    }
  }
}
BENCHMARK(BM_Replication);

}  // namespace
