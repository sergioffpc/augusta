#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/policy_actions.h"
#include "augusta/protocol.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "benchmark_scene.h"
#include "policy_loader.h"

// One SimulationWorld::Tick (ADR-0023), all eight phases, in a full Match: the
// server's whole tick but for its I/O. The players stand in a file, facing
// down it, walk back and forth together and hold fire, reloading when their
// magazine runs out: every round but the front player's strikes the head of
// the player ahead. So Movement, WeaponHandling, Ballistics, HitDetection and
// Damage have work every tick, and Scripts/Behaviours runs the example
// scenario's Game policy, loaded from its golden server pack. Each hit takes
// too little of a health too large for anyone to die, so every tick does the
// same work and the Match never ends.
namespace {

using augusta::assets::Pack;
using augusta::benchmarks::kBodySquares;
using augusta::command::Command;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::simulation::Character;
using augusta::simulation::CharacterHitbox;
using augusta::simulation::EntityId;
using augusta::simulation::PlayerCommand;
using augusta::simulation::PlayerIdentity;
using augusta::simulation::SessionId;
using augusta::simulation::State;

const std::filesystem::path kExamplePacks{AUGUSTA_EXAMPLE_PACKS};

constexpr std::uint8_t kTickRate = 60;
constexpr float kTick = 1.0F / kTickRate;
// A full Match (US-02).
constexpr std::uint8_t kPlayers = augusta::protocol::kMaxPlayers;
// How long players walk one way before turning back, and how long they settle
// and shoot before timing starts, so the bullets in flight are at their usual
// count.
constexpr std::uint64_t kTicksPerWalk = 120;
constexpr int kWarmUpTicks = 120;

// The example scenario's rifle and ammunition (parameters.lua), without its
// recoil, which would turn the rounds off the player ahead, and with a health
// no number of hits a benchmark runs can take away.
Parameters ExampleParameters() {
  Parameters parameters;
  parameters.rifle.rounds_per_minute = 600.0F;
  parameters.rifle.muzzle_velocity = augusta::benchmarks::kMuzzleVelocity;
  parameters.rifle.reload_seconds = 2.5F;
  parameters.rifle.magazine_capacity = 30;
  parameters.ammo.gravity = 9.81F;
  parameters.ammo.max_range = 1000.0F;
  parameters.ammo.damage = {.head = 1.0F, .torso = 1.0F, .limb = 1.0F};
  parameters.starting_health = 1'000'000.0F;
  parameters.player_count = kPlayers;
  return parameters;
}

Character SquaresCharacter() {
  Character character{.eye = Vec3(0.0F, 1.5F, 0.0F), .hitboxes = {}};
  for (const auto& square : kBodySquares) {
    character.hitboxes.push_back(
        CharacterHitbox{.part = square.part, .triangles = augusta::benchmarks::Triangles(square, Vec3(0.0F))});
  }
  return character;
}

// The example's Game policy, as the server loads it; nullopt if it does not load.
std::optional<augusta::scripting::Engine> ExamplePolicy() {
  const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(kExamplePacks / "test.pub");
  if (!public_key.has_value()) {
    return std::nullopt;
  }
  const auto pack = Pack::Load(kExamplePacks / "server.pack", *public_key);
  if (!pack.has_value()) {
    return std::nullopt;
  }
  auto policy = augusta::server::LoadPolicy(*pack);
  if (!policy.has_value()) {
    return std::nullopt;
  }
  return std::move(*policy);
}

// Each player's command for the tick after previous: walking along Z, which way
// by tick, and firing down -Z.
std::vector<PlayerCommand> Commands(const State& previous) {
  const bool forward = (previous.tick / kTicksPerWalk) % 2 == 0;
  std::vector<PlayerCommand> commands;
  for (const auto& body : previous.bodies) {
    Command command;
    command.movement.direction = Vec3(0.0F, 0.0F, forward ? -1.0F : 1.0F);
    command.fire = true;
    command.reload = body.rifle.rounds == 0;
    command.seen_tick = previous.tick;
    commands.push_back(PlayerCommand{.entity = body.entity, .command = command});
  }
  return commands;
}

void BM_SimulationTick(benchmark::State& state) {
  auto policy = ExamplePolicy();
  if (!policy.has_value()) {
    state.SkipWithError("the example's Game policy does not load");
    return;
  }
  augusta::simulation::World world(ExampleParameters(), kTickRate, std::move(*policy));
  if (!world.AddCollisionMesh(augusta::benchmarks::Floor()).has_value()) {
    state.SkipWithError("the floor does not build");
    return;
  }
  // In a file along -Z, far enough apart that no one walks into another.
  for (std::uint32_t player = 0; player < kPlayers; ++player) {
    world.AddPlayer(static_cast<EntityId>(player + 1), Vec3(0.0F, 0.5F, -3.0F * static_cast<float>(player)),
                    SquaresCharacter(),
                    PlayerIdentity{.session = static_cast<SessionId>(player + 1), .character = std::string()});
  }

  State previous = world.Tick({}, kTick).state;
  bool hit = false;
  for (int tick = 0; tick < kWarmUpTicks; ++tick) {
    auto result = world.Tick(Commands(previous), kTick);
    hit = hit || !result.state.hits.empty();
    if (!result.actions.empty()) {
      state.SkipWithError("the Game policy ended the Match");
      return;
    }
    previous = std::move(result.state);
  }
  if (!hit) {
    state.SkipWithError("no round hit a player");
    return;
  }
  for (auto _ : state) {
    previous = world.Tick(Commands(previous), kTick).state;
  }
}
BENCHMARK(BM_SimulationTick);

}  // namespace
