#ifndef AUGUSTA_TESTS_BENCHMARK_MATCH_H_
#define AUGUSTA_TESTS_BENCHMARK_MATCH_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>
#include <boost/uuid/time_generator_v7.hpp>
#include <boost/uuid/uuid.hpp>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/hitbox.h"
#include "augusta/limits.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/policy.h"
#include "augusta/policy_actions.h"
#include "augusta/policy_loader.h"
#include "augusta/simulation.h"
#include "benchmark_scene.h"
#include "character_fixture.h"

// The full Match the server-side benchmarks run (ADR-0023): the players stand
// in a file, facing down it, walk back and forth together and hold fire,
// reloading when their magazine runs out: every round but the front player's
// strikes the head of the player ahead. So Movement, WeaponHandling,
// Ballistics, HitDetection and Damage have work every tick, and
// Scripts/Behaviours runs the example scenario's Game policy, loaded from its
// golden server pack. Each hit takes too little of a health too large for
// anyone to die, so every tick does the same work and the Match never ends.
// The open range is the same Match with the players abreast, skeletal: every
// round flies its full range past the others, so Lag compensation judges each
// round in flight against every other player every tick. Needs
// AUGUSTA_EXAMPLE_PACKS, the directory of the golden packs.
namespace augusta::benchmarks {

inline constexpr std::uint8_t kTickRate = 60;
inline constexpr float kTick = 1.0F / kTickRate;
// A full Match (US-02).
inline constexpr std::uint8_t kPlayers = limits::kMaxPlayers;
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
  parameters.ammo.flight.gravity = 9.81F;
  parameters.ammo.flight.max_range = 1000.0F;
  parameters.ammo.damage = {.head = 1.0F, .torso = 1.0F, .limb = 1.0F};
  parameters.starting_health = 1'000'000.0F;
  parameters.player_count = kPlayers;
  return parameters;
}

inline simulation::Character SquaresCharacter() {
  simulation::Character character{.eye = math::Vec3(0.0F, 1.5F, 0.0F), .hitboxes = {}};
  for (const auto& square : kBodySquares) {
    character.hitboxes.push_back(
        hitbox::CharacterHitbox{.part = square.part, .triangles = Triangles(square, math::Vec3(0.0F))});
  }
  return character_fixture::Rigged(character);
}

// The example soldier's Joint count (characters/soldier.usda).
inline constexpr std::size_t kSoldierJoints = 12;

// SquaresCharacter on a Skeleton of joint_count Joints, its Hitboxes bound
// across them: under its Root, a chain of Joints for each of its body squares,
// the square bound to the chain's last, and the two arms Rig hangs, so the
// Hitbox history keeps every Joint but the arms'. Each chain rises in even
// steps to its square's center, the square on its last Joint, so it is where
// SquaresCharacter has it and as far from the root as a soldier's Joints hold
// their hitboxes. joint_count is more than the arms' Joints and the Root.
inline simulation::Character SkeletalCharacter(std::size_t joint_count) {
  // The Joints Rig's two arms add (clips_fixture::RigArm).
  constexpr std::size_t kArmJoints = 6;
  assert(joint_count > kArmJoints + 1);
  simulation::Character character{.eye = math::Vec3(0.0F, 1.5F, 0.0F), .hitboxes = {}};
  const std::size_t chained = joint_count - kArmJoints - 1;
  for (std::size_t square = 0; square < kBodySquares.size(); ++square) {
    std::uint32_t parent = 0;
    // The chains share chained Joints out, the first ones a Joint more.
    const std::size_t length = (chained / kBodySquares.size()) + (square < chained % kBodySquares.size() ? 1 : 0);
    const float height = kBodySquares.at(square).height;
    const math::Vec3 step(0.0F, length > 0 ? height / static_cast<float>(length) : 0.0F, 0.0F);
    for (std::size_t link = 0; link < length; ++link) {
      character.skeleton.joints.push_back({.name = character.skeleton.joints.at(parent).name + "/Chain" +
                                                   std::to_string(square) + "_" + std::to_string(link),
                                           .parent = parent,
                                           .translation = step});
      character.skeleton.rest_transforms.push_back(
          character.skeleton.rest_transforms.at(parent) *
          math::ToMat4(step, math::Quat(1.0F, 0.0F, 0.0F, 0.0F), math::Vec3(1.0F)));
      parent = static_cast<std::uint32_t>(character.skeleton.joints.size() - 1);
    }
    // In its last Joint's space, which a chain of none leaves the Root's.
    const math::Vec3 feet = length > 0 ? math::Vec3(0.0F, -height, 0.0F) : math::Vec3(0.0F);
    character.hitboxes.push_back(hitbox::CharacterHitbox{
        .part = kBodySquares.at(square).part, .triangles = Triangles(kBodySquares.at(square), feet), .joint = parent});
  }
  return character_fixture::Rigged(character);
}

// The example's Game policy, as the server loads it; nullopt if it does not load.
inline std::optional<policy::Policy> ExamplePolicy() {
  const std::filesystem::path packs{AUGUSTA_EXAMPLE_PACKS};
  const auto public_key = assets::ReadEd25519PublicKeyFile(packs / "test.pub");
  if (!public_key.has_value()) {
    return std::nullopt;
  }
  const auto pack = assets::Pack::Load((packs / "server").replace_extension(assets::kPackExtension), *public_key);
  if (!pack.has_value()) {
    return std::nullopt;
  }
  auto policy = policy::LoadPolicy(*pack);
  if (!policy.has_value()) {
    return std::nullopt;
  }
  return std::move(*policy);
}

// How the players' commands report their Seen time.
enum class SeenTimes : std::uint8_t {
  // All the last State, as of clients that each drew it just as it arrived:
  // every round of a tick is judged at one moment.
  kLastState,
  // Each a different fraction past the State before it, as of clients that
  // draw between States at their own times: no two rounds of a tick are judged
  // at the same moment.
  kBetweenStates,
};

// Each player's command for the tick after previous: walking along Z, which way
// by tick, and firing down -Z.
inline std::vector<simulation::PlayerCommand> MatchCommands(const simulation::State& previous,
                                                            SeenTimes seen_times = SeenTimes::kLastState) {
  const bool forward = (previous.tick / kTicksPerWalk) % 2 == 0;
  std::vector<simulation::PlayerCommand> commands;
  for (const auto& body : previous.bodies) {
    command::Command command;
    command.movement.direction = math::Vec3(0.0F, 0.0F, forward ? -1.0F : 1.0F);
    command.fire = true;
    command.reload = body.rifle.rounds == 0;
    command.seen_tick = previous.tick;
    if (seen_times == SeenTimes::kBetweenStates) {
      command.seen_tick = previous.tick - 1;
      command.seen_fraction =
          static_cast<float>((std::to_underlying(body.entity) + previous.tick) % kPlayers) / kPlayers;
    }
    commands.push_back(simulation::PlayerCommand{.entity = body.entity, .command = command});
  }
  return commands;
}

// The Match warmed up, ready to time: its world, the State of its last tick
// and how its commands report their Seen time.
struct FullMatch {
  simulation::World world;
  simulation::State last_state;
  SeenTimes seen_times = SeenTimes::kLastState;
};

// Where a Match stands its players.
enum class Layout : std::uint8_t {
  // In a file along -Z, 3 m apart, far enough that no one walks into another:
  // every round but the front player's strikes the head of the player ahead.
  kFile,
  // Abreast along X, 4 m apart, farther than any of their hitboxes reach from
  // their roots: every round flies its full range, down -Z, past the others.
  kOpenRange,
};

// The full Match of character, its players standing as layout has them,
// warmed up with commands reporting seen_times; nullopt, with benchmark
// skipped saying why, if it cannot be set up or does not do the work it is
// meant to: a hit in a file, none and rounds in flight on the open range.
inline std::optional<FullMatch> StartMatch(benchmark::State& benchmark, SeenTimes seen_times, Layout layout,
                                           const simulation::Character& character) {
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
  const math::Vec3 apart = layout == Layout::kFile ? math::Vec3(0.0F, 0.0F, -3.0F) : math::Vec3(4.0F, 0.0F, 0.0F);
  for (std::uint32_t player = 0; player < kPlayers; ++player) {
    world.AddPlayer(
        static_cast<simulation::EntityId>(player + 1),
        math::Vec3(0.0F, 0.5F, 0.0F) + (apart * static_cast<float>(player)), character,
        simulation::PlayerIdentity{.session = boost::uuids::time_generator_v7{}(), .character = std::string()});
  }

  simulation::State previous = world.Tick({}, kTick).state;
  bool hit = false;
  for (int tick = 0; tick < kWarmUpTicks; ++tick) {
    auto result = world.Tick(MatchCommands(previous, seen_times), kTick);
    hit = hit || !result.state.hits.empty();
    if (!result.actions.empty()) {
      benchmark.SkipWithError("the Game policy ended the Match");
      return std::nullopt;
    }
    previous = std::move(result.state);
  }
  if (layout == Layout::kFile && !hit) {
    benchmark.SkipWithError("no round hit a player");
    return std::nullopt;
  }
  if (layout == Layout::kOpenRange && (hit || previous.bullets_in_flight == 0)) {
    benchmark.SkipWithError("the rounds do not fly their range past the players");
    return std::nullopt;
  }
  return FullMatch{.world = std::move(world), .last_state = std::move(previous), .seen_times = seen_times};
}

// The full Match in a file, of SquaresCharacter, as StartMatch.
inline std::optional<FullMatch> StartFullMatch(benchmark::State& benchmark,
                                               SeenTimes seen_times = SeenTimes::kLastState) {
  return StartMatch(benchmark, seen_times, Layout::kFile, SquaresCharacter());
}

}  // namespace augusta::benchmarks

#endif  // AUGUSTA_TESTS_BENCHMARK_MATCH_H_
