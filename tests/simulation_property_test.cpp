#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <set>
#include <vector>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "augusta/command.h"
#include "augusta/input.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/simulation.h"

// Property-based tests of the simulation (ADR-0013), through the Worlds' public
// interfaces: a player's stamina stays a fraction and its magazine within its
// capacity whatever it is commanded to do, no rifle outpaces its fire rate, and
// a client whose prediction diverged from the server converges on the
// server's state once the server stops diverging. RC_PARAMS sets the case count
// at run time; pull requests run the default 100.
namespace augusta::command {

// How RapidCheck prints a failing case, found by argument-dependent lookup.
void showValue(const Command& command, std::ostream& out) {
  const math::Vec3& direction = command.movement.direction;
  out << "{direction (" << direction.x << ", " << direction.y << ", " << direction.z << "), sprint "
      << command.movement.sprint << ", stance " << static_cast<int>(command.movement.desired_stance) << ", yaw "
      << command.yaw << ", pitch " << command.pitch << ", ads " << command.ads << ", fire " << command.fire
      << ", reload " << command.reload << "}";
}

}  // namespace augusta::command

namespace augusta::physics {

void showValue(const StaminaConfig& config, std::ostream& out) {
  out << "{deplete " << config.deplete_per_second << ", regen " << config.regen_per_second << ", forced_walk_below "
      << config.forced_walk_below << "}";
}

}  // namespace augusta::physics

namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::CollisionMesh;
using augusta::physics::StaminaConfig;
using augusta::physics::Stance;
using augusta::prediction::Acknowledgement;

// Every reconciliation logs a line at INFO, and a case reconciles on most of
// its ticks: at the nightly's case count, that is millions of lines no one
// reads. A failing case says what went wrong without them.
class QuietLogs : public ::testing::Environment {
 public:
  void SetUp() override { augusta::logging::SetLogLevel(augusta::logging::Severity::kWarn); }
};

[[maybe_unused]] const ::testing::Environment* const kQuietLogs = ::testing::AddGlobalTestEnvironment(new QuietLogs);

constexpr float kTick = 1.0F / 60.0F;
constexpr std::size_t kMaxCommands = 240;  // Four seconds of input.
constexpr float kPi = 3.14159265F;
constexpr augusta::simulation::EntityId kPlayer = static_cast<augusta::simulation::EntityId>(1);
const Vec3 kSpawn(0.0F, 0.0F, 0.0F);
const Vec3 kEye(0.0F, 1.7F, 0.0F);

CollisionMesh Floor() {
  constexpr float kExtent = 100.0F;
  return CollisionMesh{.points = {Vec3(-kExtent, 0.0F, -kExtent), Vec3(-kExtent, 0.0F, kExtent),
                                  Vec3(kExtent, 0.0F, kExtent), Vec3(kExtent, 0.0F, -kExtent)},
                       .indices = {0, 1, 2, 0, 2, 3}};
}

// A Command the client's sampler (augusta::input::Input::Sample) could build:
// each movement key held or not, so a direction that is one of eight unit
// headings turned by the view's yaw, or none; the yaw within one turn and the
// pitch within kMaxLookPitch; the stance and every button held or not.
rc::Gen<Command> RealCommand() {
  const auto axis = rc::gen::element(-1.0F, 0.0F, 1.0F);
  const auto yaw = rc::gen::map(rc::gen::inRange(-179'999, 180'001),
                                [](int millidegrees) { return static_cast<float>(millidegrees) * kPi / 180'000.0F; });
  const auto pitch = rc::gen::map(rc::gen::inRange(-1000, 1001), [](int permille) {
    return static_cast<float>(permille) * augusta::input::kMaxLookPitch / 1000.0F;
  });
  const auto stance = rc::gen::element(Stance::kStanding, Stance::kCrouching, Stance::kProne);
  return rc::gen::apply(
      [](float forward, float right, float yaw, float pitch, bool sprint, Stance stance, bool ads, bool fire,
         bool reload) {
        Command command;
        command.movement.direction =
            augusta::math::Normalize(augusta::command::ViewRotation(yaw, 0.0F) * Vec3(right, 0.0F, -forward));
        command.movement.sprint = sprint;
        command.movement.desired_stance = stance;
        command.yaw = yaw;
        command.pitch = pitch;
        command.ads = ads;
        command.fire = fire;
        command.reload = reload;
        return command;
      },
      axis, axis, yaw, pitch, rc::gen::arbitrary<bool>(), stance, rc::gen::arbitrary<bool>(),
      rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>());
}

// Between min_size and kMaxCommands of them, one per tick.
rc::Gen<std::vector<Command>> Commands(std::size_t min_size) {
  return rc::gen::mapcat(rc::gen::inRange<std::size_t>(min_size, kMaxCommands + 1), [](std::size_t size) {
    return rc::gen::container<std::vector<Command>>(size, RealCommand());
  });
}

// Stamina rules a server could be given: a full bar lasting from a second to
// forever, regaining from not at all to within a second, and a recovery
// threshold anywhere in the bar.
rc::Gen<StaminaConfig> Stamina() {
  const auto fraction = [](int low, int high) {
    return rc::gen::map(rc::gen::inRange(low, high + 1),
                        [](int permille) { return static_cast<float>(permille) / 1000.0F; });
  };
  return rc::gen::build<StaminaConfig>(rc::gen::set(&StaminaConfig::deplete_per_second, fraction(0, 1000)),
                                       rc::gen::set(&StaminaConfig::regen_per_second, fraction(0, 1000)),
                                       rc::gen::set(&StaminaConfig::forced_walk_below, fraction(0, 999)));
}

RC_GTEST_PROP(SimulationPropertyTest, StaminaStaysWithinTheBarWhateverThePlayerDoes, ()) {
  const StaminaConfig stamina = *Stamina();
  const std::vector<Command> commands = *Commands(1);

  augusta::simulation::World world(augusta::parameters::Parameters{.stamina = stamina});
  RC_ASSERT(world.AddCollisionMesh(Floor()).has_value());
  world.AddPlayer(kPlayer, kSpawn, kEye);

  for (const Command& command : commands) {
    const auto state = world.Tick({{.entity = kPlayer, .command = command}}, kTick);
    RC_ASSERT(state.bodies.size() == 1U);
    RC_ASSERT(state.bodies.front().body.stamina >= 0.0F);
    RC_ASSERT(state.bodies.front().body.stamina <= 1.0F);
  }
}

// A rifle a scenario could give: a magazine of 1 to 30 rounds, from one round a
// second to more than one a tick, and a reload of no time up to a second.
rc::Gen<augusta::parameters::Rifle> Rifle() {
  using augusta::parameters::Rifle;
  return rc::gen::apply(
      [](int capacity, int rounds_per_minute, int reload_milliseconds) {
        Rifle rifle;
        rifle.magazine_capacity = static_cast<std::uint8_t>(capacity);
        rifle.rounds_per_minute = static_cast<float>(rounds_per_minute);
        rifle.reload_seconds = static_cast<float>(reload_milliseconds) / 1000.0F;
        rifle.muzzle_velocity = 600.0F;
        return rifle;
      },
      rc::gen::inRange(1, 31), rc::gen::inRange(60, 6001), rc::gen::inRange(0, 1001));
}

// Whatever a client sends, fire and reload on every tick included, the server
// keeps the magazine and the fire rate (US-07, US-08). The rounds fired in any
// stretch of ticks are at most what the fire interval fits in it, and one more
// for the round that opens it.
RC_GTEST_PROP(SimulationPropertyTest, TheMagazineStaysWithinItsCapacityAndNoWindowOutpacesTheFireRate, ()) {
  augusta::parameters::Parameters parameters;
  parameters.rifle = *Rifle();
  parameters.ammo.max_range = 50.0F;
  const std::vector<Command> commands = *Commands(1);

  augusta::simulation::World world(parameters);
  RC_ASSERT(world.AddCollisionMesh(Floor()).has_value());
  world.AddPlayer(kPlayer, kSpawn, kEye);

  std::vector<std::size_t> fired;  // The ticks a round was fired on.
  for (std::size_t i = 0; i < commands.size(); ++i) {
    const auto state = world.Tick({{.entity = kPlayer, .command = commands[i]}}, kTick);
    RC_ASSERT(state.bodies.size() == 1U);
    // Unsigned, so a magazine taken below zero would be far above its capacity.
    RC_ASSERT(state.bodies.front().rifle.rounds <= parameters.rifle.magazine_capacity);
    RC_ASSERT(state.shots.size() <= 1U);
    if (!state.shots.empty()) {
      fired.push_back(i);
    }
  }

  // Rounding, and the 0.1 ms within which a round counts as ready.
  constexpr float kTolerance = 0.05F;
  const float rounds_per_tick = parameters.rifle.rounds_per_minute / 60.0F * kTick;
  for (std::size_t first = 0; first < fired.size(); ++first) {
    for (std::size_t last = first; last < fired.size(); ++last) {
      const auto rounds = static_cast<float>(last - first + 1);
      const auto ticks = static_cast<float>(fired[last] - fired[first] + 1);
      RC_ASSERT(rounds <= (ticks * rounds_per_tick) + 1.0F + kTolerance);
    }
  }
}

// How far apart the client and the server may end: Reconciliation leaves a
// divergence under a millimetre uncorrected (ADR-0004), and the two worlds
// step the same body in separate PhysX scenes.
constexpr float kConvergedPosition = 0.005F;
constexpr float kConvergedStamina = 0.001F;

// The client predicts every command while the server loses some of them,
// moving the player for a lost one with no command, as it does when none
// arrives; each answer reaches the client delay ticks after the command it
// answers. Losses stop delay ticks before the end, so the client's newest
// answer, and every command it replays after it, followed what the server did.
RC_GTEST_PROP(SimulationPropertyTest, AClientThatDivergedConvergesOnTheServersState, ()) {
  const StaminaConfig stamina = *Stamina();
  const auto delay = *rc::gen::inRange<std::size_t>(1, 7);  // A round trip of up to 100 ms.
  const std::vector<Command> commands = *Commands(delay + 1);
  const auto lost = *rc::gen::container<std::set<std::size_t>>(rc::gen::inRange<std::size_t>(0, commands.size()));
  const std::size_t losses_end = commands.size() - delay - 1;

  augusta::simulation::World server(augusta::parameters::Parameters{.stamina = stamina});
  RC_ASSERT(server.AddCollisionMesh(Floor()).has_value());
  server.AddPlayer(kPlayer, kSpawn, kEye);

  augusta::prediction::World client;
  RC_ASSERT(client.AddCollisionMesh(Floor()).has_value());
  client.Start(kSpawn, augusta::parameters::Parameters{.stamina = stamina});

  std::vector<Acknowledgement> answers;  // answers[i] is the server's to commands[i].
  BodyState predicted{};
  for (std::size_t i = 0; i < commands.size(); ++i) {
    const auto sequence = static_cast<std::uint32_t>(i + 1);
    std::vector<augusta::simulation::PlayerCommand> received;
    if (i >= losses_end || !lost.contains(i)) {
      received.push_back({.entity = kPlayer, .command = commands[i]});
    }
    answers.push_back({.sequence = sequence, .body = server.Tick(received, kTick).bodies.front().body});

    std::optional<Acknowledgement> answer;
    if (i >= delay) {
      answer = answers[i - delay];
    }
    predicted = client.Tick(commands[i], sequence, answer, kTick).local_body;
  }

  const BodyState& authoritative = answers.back().body;
  RC_ASSERT(augusta::math::Length(predicted.position - authoritative.position) < kConvergedPosition);
  RC_ASSERT(std::abs(predicted.stamina - authoritative.stamina) < kConvergedStamina);
  RC_ASSERT(predicted.stance == authoritative.stance);
  RC_ASSERT(predicted.exhausted == authoritative.exhausted);
}

}  // namespace
