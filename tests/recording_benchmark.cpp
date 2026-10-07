#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <utility>

#include <benchmark/benchmark.h>

#include "augusta/simulation.h"
#include "augusta/version.h"
#include "benchmark_match.h"
#include "recording.h"

// The full Match's tick (benchmark_match.h) as server::Host runs it with a
// recording asked for (ADR-0048): through RecordedSimulation, each tick's
// record written to a file, as Host writes it, on the Simulation thread. The
// difference from BM_SimulationTick is what recording costs the tick.
namespace {

using augusta::benchmarks::kTick;
using augusta::benchmarks::MatchCommands;
using augusta::server::RecordedSimulation;
using augusta::server::Recorder;
using augusta::server::RecordingHeader;

void BM_RecordedSimulationTick(benchmark::State& state) {
  auto match = augusta::benchmarks::StartFullMatch(state);
  if (!match.has_value()) {
    return;
  }
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "augusta_benchmark.rec";
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
      state.SkipWithError("the recording file cannot be written");
      return;
    }
    // Joining the Match warmed up: what the timed ticks write is the same
    // either way, a Match start being one tick's.
    RecordedSimulation simulation(
        std::move(match->world), Recorder(file, RecordingHeader{.engine_version = std::string(augusta::EngineVersion()),
                                                                .server_pack = {},
                                                                .tick_rate_hz = augusta::benchmarks::kTickRate}));
    augusta::simulation::State previous = std::move(match->last_state);
    for (auto _ : state) {
      previous = simulation.Tick(MatchCommands(previous), kTick).state;
    }
  }
  std::filesystem::remove(path);
}
BENCHMARK(BM_RecordedSimulationTick);

}  // namespace
