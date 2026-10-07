#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/physics.h"

// One Authoritative State's ingestion by remote interpolation (ADR-0024): what
// PresentationWorld does on each frame a newer one has arrived - Record every
// remote body at its tick's server time, then Sync to the bodies it lists.
// Timed over a growing number of dynamic bodies, so the time per body shows
// whether ingestion stays linear in them rather than searching every buffered
// entity for each one.
namespace {

using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::presentation::EntityId;
using augusta::presentation::RemoteInterpolator;

constexpr double kTickDuration = 1.0 / 60.0;

void BM_RemoteInterpolationIngest(benchmark::State& state) {
  const auto body_count = static_cast<std::uint32_t>(state.range(0));
  std::vector<EntityId> present;
  present.reserve(body_count);
  for (std::uint32_t id = 1; id <= body_count; ++id) {
    present.push_back(static_cast<EntityId>(id));
  }
  RemoteInterpolator interpolator;
  BodyState body_state;
  body_state.position = Vec3(1.0F, 0.0F, -2.0F);
  std::uint64_t tick = 0;
  for (auto _ : state) {
    const double server_time = static_cast<double>(tick++) * kTickDuration;
    for (const EntityId entity : present) {
      interpolator.Record(entity, server_time, body_state, 0.0F);
    }
    interpolator.Sync(present);
  }
  benchmark::DoNotOptimize(interpolator);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(body_count));
}
BENCHMARK(BM_RemoteInterpolationIngest)->RangeMultiplier(4)->Range(8, 2048);

}  // namespace
