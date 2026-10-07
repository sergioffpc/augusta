#ifndef AUGUSTA_TESTS_BENCHMARK_MATCH_H_
#define AUGUSTA_TESTS_BENCHMARK_MATCH_H_

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

// The full Match the server-side benchmarks run (ADR-0023): the players stand
// in a file, facing down it, walk back and forth together and hold fire,
// reloading when their magazine runs out: every round but the front player's
// strikes the head of the player ahead. So Movement, WeaponHandling,
// Ballistics, HitDetection and Damage have work every tick, and
// Scripts/Behaviours runs the example scenario's Game policy, loaded from its
// golden server pack. Each hit takes too little of a health too large for
// anyone to die, so every tick does the same work and the Match never ends.
// Needs AUGUSTA_EXAMPLE_PACKS, the directory of the golden packs.
namespace augusta::benchmarks {

inline constexpr std::uint8_t kTickRate = 60;
inline constexpr float kTick = 1.0F / kTickRate;
// A full Match (US-02).
inline constexpr std::uint8_t kPlayers = protocol::kMaxPlayers;
// How long players walk one way before turning back, and how long they settle
// and shoot before timing starts, so the bullets in flight are at their usual
// count.
inline constexpr std::uint64_t kTicksPerWalk = 120;
inline constexpr int kWarmUpTicks = 120;

// The example scenario's rifle and ammunition (parameters.lua), without its
// recoil, which would turn the rounds off the player ahead, and with a health
// no number of hits a benchmark runs can take away.
inline parameters::Parameters MatchParameters() {
  parameters::Parameters parameters;
  parameters.rifle.rounds_per_minute = 600.0F;
  parameters.rifle.muzzle_velocity = kMuzzleVelocity;
  parameters.rifle.reload_seconds = 2.5F;
  parameters.rifle.magazine_capacity = 30;
  parameters.ammo.gravity = 9.81F;
  parameters.ammo.max_range = 1000.0F;
  parameters.ammo.damage = {.head = 1.0F, .torso = 1.0F, .limb = 1.0F};
  parameters.starting_health = 1'000'000.0F;
  parameters.player_count = kPlayers;
  return parameters;
}

inline simulation::Character SquaresCharacter() {
  simulation::Character character{.eye = math::Vec3(0.0F, 1.5F, 0.0F), .hitboxes = {}};
  for (const auto& square : kBodySquares) {
    character.hitboxes.push_back(
        simulation::CharacterHitbox{.part = square.part, .triangles = Triangles(square, math::Vec3(0.0F))});
  }
  return character;
}

// The example's Game policy, as the server loads it; nullopt if it does not load.
inline std::optional<scripting::Engine> ExamplePolicy() {
  const std::filesystem::path packs{AUGUSTA_EXAMPLE_PACKS};
  const auto public_key = assets::ReadEd25519PublicKeyFile(packs / "test.pub");
  if (!public_key.has_value()) {
    return std::nullopt;
  }
  const auto pack = assets::Pack::Load(packs / "server.pack", *public_key);
  if (!pack.has_value()) {
    return std::nullopt;
  }
  auto policy = server::LoadPolicy(*pack);
  if (!policy.has_value()) {
    return std::nullopt;
  }
  return std::move(*policy);
}

// Each player's command for the tick after previous: walking along Z, which way
// by tick, and firing down -Z.
inline std::vector<simulation::PlayerCommand> MatchCommands(const simulation::State& previous) {
  const bool forward = (previous.tick / kTicksPerWalk) % 2 == 0;
  std::vector<simulation::PlayerCommand> commands;
  for (const auto& body : previous.bodies) {
    command::Command command;
    command.movement.direction = math::Vec3(0.0F, 0.0F, forward ? -1.0F : 1.0F);
    command.fire = true;
    command.reload = body.rifle.rounds == 0;
    command.seen_tick = previous.tick;
    commands.push_back(simulation::PlayerCommand{.entity = body.entity, .command = command});
  }
  return commands;
}

// The Match warmed up, ready to time: its world and the State of its last tick.
struct FullMatch {
  simulation::World world;
  simulation::State last_state;
};

// The full Match, warmed up; nullopt, with benchmark skipped saying why, if it
// cannot be set up or does not do the work it is meant to.
inline std::optional<FullMatch> StartFullMatch(benchmark::State& benchmark) {
  auto policy = ExamplePolicy();
  if (!policy.has_value()) {
    benchmark.SkipWithError("the example's Game policy does not load");
    return std::nullopt;
  }
  simulation::World world(MatchParameters(), kTickRate, std::move(*policy));
  if (!world.AddCollisionMesh(Floor()).has_value()) {
    benchmark.SkipWithError("the floor does not build");
    return std::nullopt;
  }
  // In a file along -Z, far enough apart that no one walks into another.
  for (std::uint32_t player = 0; player < kPlayers; ++player) {
    world.AddPlayer(static_cast<simulation::EntityId>(player + 1),
                    math::Vec3(0.0F, 0.5F, -3.0F * static_cast<float>(player)), SquaresCharacter(),
                    simulation::PlayerIdentity{.session = static_cast<simulation::SessionId>(player + 1),
                                               .character = std::string()});
  }

  simulation::State previous = world.Tick({}, kTick).state;
  bool hit = false;
  for (int tick = 0; tick < kWarmUpTicks; ++tick) {
    auto result = world.Tick(MatchCommands(previous), kTick);
    hit = hit || !result.state.hits.empty();
    if (!result.actions.empty()) {
      benchmark.SkipWithError("the Game policy ended the Match");
      return std::nullopt;
    }
    previous = std::move(result.state);
  }
  if (!hit) {
    benchmark.SkipWithError("no round hit a player");
    return std::nullopt;
  }
  return FullMatch{.world = std::move(world), .last_state = std::move(previous)};
}

}  // namespace augusta::benchmarks

#endif  // AUGUSTA_TESTS_BENCHMARK_MATCH_H_
