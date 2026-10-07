#include <benchmark/benchmark.h>

#include "benchmark_match.h"

// One SimulationWorld::Tick (ADR-0023), all eight phases, in a full Match: the
// server's whole tick but for its I/O (benchmark_match.h).
namespace {

using augusta::benchmarks::kTick;
using augusta::benchmarks::MatchCommands;

void BM_SimulationTick(benchmark::State& state) {
  auto match = augusta::benchmarks::StartFullMatch(state);
  if (!match.has_value()) {
    return;
  }
  for (auto _ : state) {
    match->last_state = match->world.Tick(MatchCommands(match->last_state), kTick).state;
  }
}
BENCHMARK(BM_SimulationTick);

}  // namespace
