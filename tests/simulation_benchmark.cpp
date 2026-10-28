#include <cstddef>
#include <optional>

#include <benchmark/benchmark.h>

#include "augusta/assets.h"
#include "benchmark_match.h"

// One SimulationWorld::Tick (ADR-0023), all eight phases, in a full Match: the
// server's whole tick but for its I/O (benchmark_match.h). On the open range,
// at the example soldier's Joint count and at the cap's, the Lag compensation
// of every round in flight against every other player.
namespace {

using augusta::benchmarks::FullMatch;
using augusta::benchmarks::kTick;
using augusta::benchmarks::MatchCommands;
using augusta::benchmarks::SeenTimes;

// Times match's ticks; skipped already if match is nullopt.
void RunSimulationTick(benchmark::State& state, std::optional<FullMatch> match) {
  if (!match.has_value()) {
    return;
  }
  for (auto _ : state) {
    match->last_state = match->world.Tick(MatchCommands(match->last_state, match->seen_times), kTick).state;
  }
}

void BM_SimulationTick(benchmark::State& state) {
  RunSimulationTick(state, augusta::benchmarks::StartFullMatch(state));
}
BENCHMARK(BM_SimulationTick);

void BM_SimulationTickBetweenStates(benchmark::State& state) {
  RunSimulationTick(state, augusta::benchmarks::StartFullMatch(state, SeenTimes::kBetweenStates));
}
BENCHMARK(BM_SimulationTickBetweenStates);

// Its argument is the players' Character's Joint count.
void BM_SimulationTickOpenRange(benchmark::State& state) {
  const auto joints = static_cast<std::size_t>(state.range(0));
  RunSimulationTick(
      state, augusta::benchmarks::StartMatch(state, SeenTimes::kLastState, augusta::benchmarks::Layout::kOpenRange,
                                             augusta::benchmarks::SkeletalCharacter(joints)));
}
BENCHMARK(BM_SimulationTickOpenRange)->Arg(augusta::benchmarks::kSoldierJoints)->Arg(augusta::assets::kMaxJoints);

}  // namespace
