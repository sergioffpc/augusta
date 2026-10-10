#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <benchmark/benchmark.h>

#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/simulation.h"
#include "benchmark_match.h"
#include "benchmark_scene.h"
#include "capture.h"
#include "match.h"
#include "replay.h"
#include "replay_server.h"

// What one Replay viewer costs a replay server's Simulation thread each tick
// (ADR-0051): its Replay's step - a SimulationWorld tick of the full Match
// benchmark_match.h runs, handed the capture's Commands through each player's
// queue - and encoding what its viewer is sent. A replay server ticks every
// Replay it runs in one tick, so replay.max_viewers of these must fit in the
// tick budget (NFR-01: 16.7 ms at 60 Hz).
namespace {

using augusta::benchmarks::kPlayers;
using augusta::benchmarks::kTicksPerWalk;
using augusta::server::Capture;
using augusta::server::CapturedCommand;
using augusta::server::CapturedJoin;
using augusta::server::CapturedMatchEnd;
using augusta::server::CaptureRecord;

constexpr const char* kCharacter = "squares";
// Long enough that a run rarely starts the Replay over.
constexpr std::uint32_t kCaptureTicks = 36'000;
// The rifle's magazine of 30 at 600 rounds a minute lasts 180 ticks.
constexpr std::uint32_t kReloadEvery = 200;

// The full Match as a capture: its players in a file along -Z, each walking
// back and forth and holding fire every tick, reloading now and then.
Capture FullMatchCapture() {
  Capture capture;
  capture.header.tick_rate_hz = augusta::benchmarks::kTickRate;
  for (std::uint8_t player = 1; player <= kPlayers; ++player) {
    capture.records.push_back(CaptureRecord{
        .offset = 0,
        .event = CapturedJoin{.player = player,
                              .session = augusta::server::SessionId{player},
                              .character = kCharacter,
                              .spawn = augusta::math::Vec3(0.0F, 0.5F, -3.0F * static_cast<float>(player - 1))}});
  }
  for (std::uint32_t offset = 0; offset < kCaptureTicks; ++offset) {
    for (std::uint8_t player = 1; player <= kPlayers; ++player) {
      augusta::command::Command command;
      command.movement.direction = augusta::math::Vec3(0.0F, 0.0F, (offset / kTicksPerWalk) % 2 == 0 ? -1.0F : 1.0F);
      command.fire = true;
      command.reload = offset % kReloadEvery == kReloadEvery - 1;
      capture.records.push_back(CaptureRecord{
          .offset = offset, .event = CapturedCommand{.player = player, .seen_offset = -1, .command = command}});
    }
  }
  capture.records.push_back(CaptureRecord{.offset = kCaptureTicks, .event = CapturedMatchEnd{}});
  return capture;
}

std::unique_ptr<augusta::server::Replay> StartReplay(const Capture& capture) {
  auto policy = augusta::benchmarks::ExamplePolicy();
  if (!policy.has_value()) {
    return nullptr;
  }
  augusta::simulation::World world(augusta::benchmarks::MatchParameters(), augusta::benchmarks::kTickRate,
                                   std::move(*policy));
  if (!world.AddCollisionMesh(augusta::benchmarks::Floor()).has_value()) {
    return nullptr;
  }
  static const std::unordered_map<std::string, augusta::simulation::Character> kCharacters = {
      {kCharacter, augusta::benchmarks::SquaresCharacter()}};
  return std::make_unique<augusta::server::Replay>(capture, std::move(world), kCharacters);
}

void BM_ReplayViewerTick(benchmark::State& state) {
  const Capture capture = FullMatchCapture();
  std::unique_ptr<augusta::server::Replay> replay = StartReplay(capture);
  if (replay == nullptr) {
    state.SkipWithError("the example's Game policy or the floor does not load");
    return;
  }
  for (auto _ : state) {
    if (replay->Ended()) {
      state.PauseTiming();
      replay = StartReplay(capture);
      state.ResumeTiming();
    }
    const augusta::server::ReplayTick tick = replay->Step();
    auto messages = augusta::server::ViewerMessages(tick);
    benchmark::DoNotOptimize(messages);
  }
}
BENCHMARK(BM_ReplayViewerTick);

}  // namespace
