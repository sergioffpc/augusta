#include <benchmark/benchmark.h>

#include "augusta/version.h"

static void BM_EngineVersion(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(augusta::EngineVersion());
  }
}
BENCHMARK(BM_EngineVersion);
