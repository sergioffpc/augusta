#include <cstddef>
#include <filesystem>
#include <vector>

#include <benchmark/benchmark.h>

#include "augusta/assets.h"
#include "augusta/audio.h"
#include "augusta/cues.h"
#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/presentation.h"
#include "augusta/primitives.h"
#include "augusta/tick.h"
#include "benchmark_scene.h"

// One render frame of PresentationWorld (ADR-0024), all five phases, in a full
// Match, handed its input as ClientRuntime hands it each frame: the newest
// Authoritative State's bodies and the Match's characters, lent to the frame's
// FrameInput. The client renders faster than the server ticks, so most frames
// are handed the same Authoritative State as the frame before
// (unchanged_state) and a few a newer one (new_state), whose bodies are
// buffered for interpolation. Without a GPU or a window: the renderer draws
// what this returns, and is judged by eye (ADR-0013). ClientRuntime converts
// the Server view only when a newer Authoritative State arrives
// (ConvertedServerView, src/client/frame_mapping.h), which builds on Windows
// only, and the nightly benchmarks on Linux, so building the newer State here
// stands in for that conversion.
namespace {

using augusta::math::Vec3;
using augusta::presentation::DynamicBody;
using augusta::presentation::EntityId;
using augusta::presentation::FrameInput;
using augusta::presentation::PlayerCharacter;
using augusta::presentation::WorldSnapshot;

constexpr double kTickDuration = 1.0 / 60.0;
constexpr std::size_t kPlayers = augusta::primitives::kMaxPlayers;
constexpr EntityId kLocal{1};
// Each body's walking speed along -Z, in meters per second.
constexpr float kWalkSpeed = 4.5F;

// Every player of a full Match standing in a file along -Z, walking down it,
// as the Authoritative State of tick tells them.
WorldSnapshot FullMatchSnapshot(augusta::tick::Tick tick) {
  WorldSnapshot snapshot{.tick = tick, .tick_duration = kTickDuration, .bodies = {}};
  const float walked = kWalkSpeed * static_cast<float>(static_cast<double>(tick) * kTickDuration);
  for (std::size_t player = 0; player < kPlayers; ++player) {
    DynamicBody body{.entity = static_cast<EntityId>(player + 1), .state = {}, .yaw = 0.0F};
    body.state.position = Vec3(0.0F, 0.0F, (-3.0F * static_cast<float>(player)) - walked);
    body.state.velocity = Vec3(0.0F, 0.0F, -kWalkSpeed);
    snapshot.bodies.push_back(body);
  }
  return snapshot;
}

std::vector<PlayerCharacter> FullMatchCharacters() {
  std::vector<PlayerCharacter> characters;
  for (std::size_t player = 0; player < kPlayers; ++player) {
    characters.push_back(PlayerCharacter{.entity = static_cast<EntityId>(player + 1), .character = "soldier"});
  }
  return characters;
}

// What ClientRuntime lends a frame of the Match while snapshot is the newest
// Authoritative State: nothing fired, hit or died since the frame before.
FrameInput FrameInputFor(const WorldSnapshot& snapshot, const std::vector<PlayerCharacter>& characters) {
  FrameInput frame;
  frame.local_entity = kLocal;
  frame.snapshot = &snapshot;
  frame.characters = characters;
  return frame;
}

void BM_RenderFrame(benchmark::State& state, bool new_state) {
  const std::filesystem::path packs{AUGUSTA_EXAMPLE_PACKS};
  const auto pack = augusta::assets::LoadVerifiedPack(packs / "client.pack", packs / "test.pub");
  if (!pack.has_value()) {
    state.SkipWithError("the example client pack does not load");
    return;
  }
  const auto cue_sounds = augusta::audio::LoadCueSounds(*pack);
  if (!cue_sounds.has_value()) {
    state.SkipWithError("the example's cue sounds do not load");
    return;
  }
  augusta::audio::Engine audio;
  augusta::presentation::World world(audio, *cue_sounds, Vec3(0.0F, 1.5F, 0.0F));
  if (!world.AddCollisionMesh(augusta::benchmarks::Floor()).has_value()) {
    state.SkipWithError("the floor does not build");
    return;
  }
  augusta::parameters::Parameters parameters;
  parameters.rifle.muzzle_velocity = augusta::benchmarks::kMuzzleVelocity;
  world.SetParameters(parameters, static_cast<float>(kTickDuration));

  const std::vector<PlayerCharacter> characters = FullMatchCharacters();
  WorldSnapshot newest = FullMatchSnapshot(1);
  // Enough Authoritative States already buffered to interpolate between.
  for (std::size_t tick = 1; tick <= augusta::presentation::kUpdatesKept; ++tick) {
    newest = FullMatchSnapshot(tick);
    world.RunFrame(FrameInputFor(newest, characters));
  }
  for (auto _ : state) {
    if (new_state) {
      newest = FullMatchSnapshot(newest.tick + 1);
    }
    benchmark::DoNotOptimize(world.RunFrame(FrameInputFor(newest, characters)));
  }
}
BENCHMARK_CAPTURE(BM_RenderFrame, unchanged_state, false);
BENCHMARK_CAPTURE(BM_RenderFrame, new_state, true);

}  // namespace
