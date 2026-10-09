#include <benchmark/benchmark.h>

#include "benchmark_match.h"

// One SimulationWorld::Tick (ADR-0023), all eight phases, in a full Match: the
// server's whole tick but for its I/O (benchmark_match.h).
namespace {

using augusta::benchmarks::kTick;
using augusta::benchmarks::MatchCommands;
using augusta::benchmarks::SeenTimes;

void RunSimulationTick(benchmark::State& state, SeenTimes seen_times) {
  auto match = augusta::benchmarks::StartFullMatch(state, seen_times);
  if (!match.has_value()) {
    return;
  }
  for (auto _ : state) {
    match->last_state = match->world.Tick(MatchCommands(match->last_state, seen_times), kTick).state;
  }
}

void BM_SimulationTick(benchmark::State& state) { RunSimulationTick(state, SeenTimes::kLastState); }
BENCHMARK(BM_SimulationTick);

void BM_SimulationTickBetweenStates(benchmark::State& state) { RunSimulationTick(state, SeenTimes::kBetweenStates); }
BENCHMARK(BM_SimulationTickBetweenStates);

}  // namespace
