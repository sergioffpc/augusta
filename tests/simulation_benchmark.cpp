#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"

// One SimulationWorld::Tick (ADR-0023), all eight phases, in a full Match: the
// server's whole tick but for its I/O. Every player walks back and forth and
// holds fire, reloading when its magazine runs out, so the Movement,
// WeaponHandling, Ballistics and HitDetection phases all have work every tick.
// The rounds fly past the other players, so every tick does the same work; no
// Game policy is loaded, so Scripts/Behaviours does nothing.
namespace {

using augusta::ballistics::BodyPart;
using augusta::ballistics::Triangle;
using augusta::command::Command;
using augusta::math::Vec3;
using augusta::parameters::Parameters;
using augusta::physics::CollisionMesh;
using augusta::simulation::Character;
using augusta::simulation::CharacterHitbox;
using augusta::simulation::EntityId;
using augusta::simulation::PlayerCommand;
using augusta::simulation::State;

constexpr std::uint8_t kTickRate = 60;
// A full Match (US-02).
constexpr std::uint8_t kPlayers = 8;
constexpr float kTick = 1.0F / kTickRate;
// How long players walk one way before turning back, and how long they settle
// and shoot before timing starts, so the bullets in flight are at their usual
// count.
constexpr std::uint64_t kTicksPerWalk = 120;
constexpr int kWarmUpTicks = 120;

// The example scenario's rifle and ammunition (parameters.lua), without its
// recoil, which would turn the rounds toward the other players, and without
// damage.
Parameters ExampleParameters() {
  Parameters parameters;
  parameters.rifle.rounds_per_minute = 600.0F;
  parameters.rifle.muzzle_velocity = 800.0F;
  parameters.rifle.reload_seconds = 2.5F;
  parameters.rifle.magazine_capacity = 30;
  parameters.ammo.gravity = 9.81F;
  parameters.ammo.max_range = 1000.0F;
  parameters.starting_health = 100.0F;
  parameters.player_count = kPlayers;
  return parameters;
}

CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                                  Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

// A square facing along Z, half_size wide each side of center.
CharacterHitbox Square(BodyPart part, Vec3 center, float half_size) {
  const Vec3 right(half_size, 0.0F, 0.0F);
  const Vec3 up(0.0F, half_size, 0.0F);
  return CharacterHitbox{
      .part = part,
      .triangles = {Triangle{.a = center - right - up, .b = center + right - up, .c = center + right + up},
                    Triangle{.a = center - right - up, .b = center + right + up, .c = center - right + up}}};
}

// A head, a torso and a limb, one square each, stacked above the feet.
Character SquaresCharacter() {
  return Character{.eye = Vec3(0.0F, 1.5F, 0.0F),
                   .hitboxes = {Square(BodyPart::kHead, Vec3(0.0F, 1.6F, 0.0F), 0.15F),
                                Square(BodyPart::kTorso, Vec3(0.0F, 1.1F, 0.0F), 0.3F),
                                Square(BodyPart::kLimb, Vec3(0.0F, 0.4F, 0.0F), 0.4F)}};
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
  augusta::simulation::World world(ExampleParameters(), kTickRate);
  if (!world.AddCollisionMesh(Floor()).has_value()) {
    state.SkipWithError("the floor does not build");
    return;
  }
  // In a row along X, far enough apart that no one walks into another.
  for (std::uint32_t player = 0; player < kPlayers; ++player) {
    world.AddPlayer(static_cast<EntityId>(player + 1), Vec3(3.0F * static_cast<float>(player), 0.5F, 0.0F),
                    SquaresCharacter());
  }

  State previous = world.Tick({}, kTick).state;
  for (int tick = 0; tick < kWarmUpTicks; ++tick) {
    previous = world.Tick(Commands(previous), kTick).state;
  }
  for (auto _ : state) {
    previous = world.Tick(Commands(previous), kTick).state;
  }
}
BENCHMARK(BM_SimulationTick);

}  // namespace
