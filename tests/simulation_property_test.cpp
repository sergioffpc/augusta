#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/input.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/policy_actions.h"
#include "augusta/prediction.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"

// Property-based tests of the simulation (ADR-0013), through the Worlds' public
// interfaces: a player's stamina stays a fraction and its magazine within its
// capacity whatever it is commanded to do, no rifle outpaces its fire rate,
// health never rises nor goes below zero, a dead player stays dead, Game policy
// ends a Match at most once and only ever with a winner alive in it, and a client whose prediction of its
// body and its rifle diverged from the server converges on the server's state
// once the server stops diverging. RC_PARAMS sets the case count
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

constexpr std::uint8_t kTickRate = 60;
constexpr float kTick = 1.0F / kTickRate;
constexpr std::size_t kMaxCommands = 240;  // Four seconds of input.
constexpr float kPi = 3.14159265F;
constexpr augusta::simulation::EntityId kPlayer = static_cast<augusta::simulation::EntityId>(1);
const Vec3 kSpawn(0.0F, 0.0F, 0.0F);
const augusta::simulation::Character kCharacter{.eye = Vec3(0.0F, 1.7F, 0.0F), .hitboxes = {}};

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

  augusta::simulation::World world(augusta::parameters::Parameters{.stamina = stamina}, kTickRate);
  RC_ASSERT(world.AddCollisionMesh(Floor()).has_value());
  world.AddPlayer(kPlayer, kSpawn, kCharacter);

  for (const Command& command : commands) {
    const auto state = world.Tick({{.entity = kPlayer, .command = command}}, kTick).state;
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

  augusta::simulation::World world(parameters, kTickRate);
  RC_ASSERT(world.AddCollisionMesh(Floor()).has_value());
  world.AddPlayer(kPlayer, kSpawn, kCharacter);

  std::vector<std::size_t> fired;  // The ticks a round was fired on.
  for (std::size_t i = 0; i < commands.size(); ++i) {
    const auto state = world.Tick({{.entity = kPlayer, .command = commands[i]}}, kTick).state;
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

// A character that is hard to miss: one torso hitbox, a wall 4 m wide and 2 m
// tall across its feet and another along them, so it is hit from any side.
augusta::simulation::Character WideTarget() {
  using augusta::ballistics::Triangle;
  const Vec3 up(0.0F, 2.0F, 0.0F);
  std::vector<Triangle> triangles;
  for (const Vec3& half : {Vec3(2.0F, 0.0F, 0.0F), Vec3(0.0F, 0.0F, 2.0F)}) {
    triangles.push_back({.a = -half, .b = half, .c = half + up});
    triangles.push_back({.a = -half, .b = half + up, .c = -half + up});
  }
  return augusta::simulation::Character{
      .eye = Vec3(0.0F, 1.7F, 0.0F),
      .hitboxes = {{.part = augusta::ballistics::BodyPart::kTorso, .triangles = std::move(triangles)}}};
}

// commands with every view brought to within a fifth of a turn of yaw toward
// and a tenth of its pitch: whatever else its player does, it looks roughly
// that way, so a good share of its rounds land.
std::vector<Command> LookingRoughly(float toward, std::vector<Command> commands) {
  for (Command& command : commands) {
    command.yaw = toward + (command.yaw * 0.2F);
    command.pitch *= 0.1F;
  }
  return commands;
}

// Two players 6 m apart and roughly facing each other, each doing whatever it
// is commanded with a rifle that fires every tick at a target hard to miss:
// health only ever goes down (it never regenerates in a Match), stops at zero,
// and reaches it once.
RC_GTEST_PROP(SimulationPropertyTest, HealthNeverRisesNeverGoesBelowZeroAndReachesZeroOnce, ()) {
  constexpr augusta::simulation::EntityId kOther = static_cast<augusta::simulation::EntityId>(2);
  augusta::parameters::Parameters parameters;
  parameters.rifle.rounds_per_minute = 3600.0F;
  parameters.rifle.magazine_capacity = 255;
  parameters.rifle.muzzle_velocity = 600.0F;
  parameters.ammo.max_range = 50.0F;
  const auto damage = rc::gen::map(rc::gen::inRange(0, 41), [](int points) { return static_cast<float>(points); });
  parameters.ammo.damage = {.head = *damage, .torso = *damage, .limb = *damage};
  parameters.starting_health = static_cast<float>(*rc::gen::inRange(1, 201));
  // The other player is down -Z, where yaw 0 looks; it looks back with half a turn.
  const std::vector<Command> commands = LookingRoughly(0.0F, *Commands(1));
  const std::vector<Command> other_commands =
      LookingRoughly(kPi, *rc::gen::container<std::vector<Command>>(commands.size(), RealCommand()));

  augusta::simulation::World world(parameters, kTickRate);
  RC_ASSERT(world.AddCollisionMesh(Floor()).has_value());
  world.AddPlayer(kPlayer, kSpawn, WideTarget());
  world.AddPlayer(kOther, Vec3(0.0F, 0.0F, -6.0F), WideTarget());

  std::map<augusta::simulation::EntityId, float> health = {{kPlayer, parameters.starting_health},
                                                           {kOther, parameters.starting_health}};
  std::map<augusta::simulation::EntityId, int> reached_zero = {{kPlayer, 0}, {kOther, 0}};
  std::size_t hits = 0;
  for (std::size_t i = 0; i < commands.size(); ++i) {
    const auto state =
        world
            .Tick({{.entity = kPlayer, .command = commands[i]}, {.entity = kOther, .command = other_commands[i]}},
                  kTick)
            .state;
    for (const auto& body : state.bodies) {
      RC_ASSERT(body.health >= 0.0F);
      RC_ASSERT(body.health <= health.at(body.entity));
      health.at(body.entity) = body.health;
    }
    for (const auto& hit : state.hits) {
      RC_ASSERT(hit.shooter != hit.target);
      RC_ASSERT(hit.health <= health.at(hit.target));
      // A player dies of the hit that takes it to zero, and has no body in the state to tell it by.
      health.at(hit.target) = hit.health;
      reached_zero.at(hit.target) += hit.reached_zero ? 1 : 0;
    }
    hits += state.hits.size();
  }
  // How many cases put the property to the test, in RapidCheck's report.
  RC_CLASSIFY(hits > 0, "players were hit");
  RC_CLASSIFY(reached_zero.at(kPlayer) + reached_zero.at(kOther) > 0, "a player reached zero");
  for (const auto& [entity, left] : health) {
    RC_ASSERT(reached_zero.at(entity) == (left <= 0.0F ? 1 : 0));
  }
}

// The same two players, each dying of the hit that takes its health to zero: a
// dead player never comes back to life, never fires and never moves (it has no
// body to) for the rest of the Match, and dies once.
RC_GTEST_PROP(SimulationPropertyTest, ADeadPlayerNeverComesBackFiresOrMoves, ()) {
  constexpr augusta::simulation::EntityId kOther = static_cast<augusta::simulation::EntityId>(2);
  augusta::parameters::Parameters parameters;
  parameters.rifle.rounds_per_minute = 3600.0F;
  parameters.rifle.magazine_capacity = 255;
  parameters.rifle.muzzle_velocity = 600.0F;
  parameters.ammo.max_range = 50.0F;
  const auto damage = rc::gen::map(rc::gen::inRange(1, 41), [](int points) { return static_cast<float>(points); });
  parameters.ammo.damage = {.head = *damage, .torso = *damage, .limb = *damage};
  parameters.starting_health = static_cast<float>(*rc::gen::inRange(1, 201));
  const std::vector<Command> commands = LookingRoughly(0.0F, *Commands(1));
  const std::vector<Command> other_commands =
      LookingRoughly(kPi, *rc::gen::container<std::vector<Command>>(commands.size(), RealCommand()));

  augusta::simulation::World world(parameters, kTickRate);
  RC_ASSERT(world.AddCollisionMesh(Floor()).has_value());
  world.AddPlayer(kPlayer, kSpawn, WideTarget());
  world.AddPlayer(kOther, Vec3(0.0F, 0.0F, -6.0F), WideTarget());

  std::set<augusta::simulation::EntityId> alive = {kPlayer, kOther};
  for (std::size_t i = 0; i < commands.size(); ++i) {
    const auto state =
        world
            .Tick({{.entity = kPlayer, .command = commands[i]}, {.entity = kOther, .command = other_commands[i]}},
                  kTick)
            .state;
    // A round fired on the tick its shooter dies was fired while it lived.
    for (const auto& shot : state.shots) {
      RC_ASSERT(alive.contains(shot.shooter));
    }
    for (const auto& death : state.deaths) {
      RC_ASSERT(alive.erase(death.victim) == 1U);
    }
    RC_ASSERT(std::set<augusta::simulation::EntityId>(state.alive.begin(), state.alive.end()) == alive);
    for (const auto& body : state.bodies) {
      RC_ASSERT(alive.contains(body.entity));
    }
  }
  RC_CLASSIFY(alive.size() < 2U, "a player died");
}

// What a Game policy hook may answer on one tick: mostly nothing, so a Match
// lasts long enough for its players to die, else a draw or a winner by
// session, which may be no player's (11 and 12 are the players'), a player's
// who has died or one alive.
rc::Gen<std::string> MatchEndAnswer() {
  return rc::gen::weightedOneOf<std::string>({{60, rc::gen::just(std::string("nil"))},
                                              {1, rc::gen::just(std::string("{draw = true}"))},
                                              {3, rc::gen::map(rc::gen::inRange(10, 14), [](int session) {
                                                 return "{winner = " + std::to_string(session) + "}";
                                               })}});
}

// An objectives script whose on_tick answers what answers holds for each tick,
// counted from 1, and nil past them.
std::string AnsweringObjectives(const std::vector<std::string>& answers) {
  std::string script = "local answers = {";
  for (const std::string& answer : answers) {
    script += answer == "nil" ? "false, " : answer + ", ";
  }
  return script + "}\nfunction on_tick(match) return answers[match.tick] or nil end\n";
}

// The same two players, in two Matches one after the other, under objectives
// that answer anything at all on any tick: whatever policy answers, at most one
// Match end takes effect per Match, and a winner is a player alive in it on the
// tick it is declared.
RC_GTEST_PROP(SimulationPropertyTest, AtMostOneMatchEndPerMatchAndAWinnerIsAlwaysAPlayerAliveInIt, ()) {
  constexpr augusta::simulation::EntityId kOther = static_cast<augusta::simulation::EntityId>(2);
  const std::map<augusta::simulation::EntityId, augusta::simulation::SessionId> sessions = {
      {kPlayer, static_cast<augusta::simulation::SessionId>(11)},
      {kOther, static_cast<augusta::simulation::SessionId>(12)}};
  augusta::parameters::Parameters parameters;
  parameters.player_count = 2;
  parameters.rifle.rounds_per_minute = 3600.0F;
  parameters.rifle.magazine_capacity = 255;
  parameters.rifle.muzzle_velocity = 600.0F;
  parameters.ammo.max_range = 50.0F;
  const auto damage = rc::gen::map(rc::gen::inRange(1, 41), [](int points) { return static_cast<float>(points); });
  parameters.ammo.damage = {.head = *damage, .torso = *damage, .limb = *damage};
  parameters.starting_health = static_cast<float>(*rc::gen::inRange(1, 101));
  const std::vector<Command> commands = LookingRoughly(0.0F, *Commands(2));
  const std::vector<Command> other_commands =
      LookingRoughly(kPi, *rc::gen::container<std::vector<Command>>(commands.size(), RealCommand()));
  const std::vector<std::string> answers =
      *rc::gen::container<std::vector<std::string>>(commands.size(), MatchEndAnswer());
  const std::size_t second_match = *rc::gen::inRange<std::size_t>(1, commands.size());
  auto policy = augusta::scripting::Engine::Load({.objectives = AnsweringObjectives(answers), .behaviours = {}});
  RC_ASSERT(policy.has_value());

  augusta::simulation::World world(parameters, kTickRate, *std::move(policy));
  RC_ASSERT(world.AddCollisionMesh(Floor()).has_value());
  int match_ends = 0;
  int winners = 0;
  bool died = false;
  for (std::size_t i = 0; i < commands.size(); ++i) {
    if (i == 0 || i == second_match) {
      world.EndMatch();
      match_ends = 0;
      world.AddPlayer(kPlayer, kSpawn, WideTarget(),
                      {.session = sessions.at(kPlayer), .character = "characters/player"});
      world.AddPlayer(kOther, Vec3(0.0F, 0.0F, -6.0F), WideTarget(),
                      {.session = sessions.at(kOther), .character = "characters/player"});
    }
    const auto result = world.Tick(
        {{.entity = kPlayer, .command = commands[i]}, {.entity = kOther, .command = other_commands[i]}}, kTick);
    died = died || !result.state.deaths.empty();
    for (const augusta::simulation::PolicyAction& action : result.actions) {
      const auto& end = std::get<augusta::simulation::MatchEnd>(action);
      RC_ASSERT(++match_ends == 1);
      if (const auto winner = end.winner) {
        ++winners;
        RC_ASSERT(std::ranges::any_of(result.state.alive, [&](auto entity) { return sessions.at(entity) == *winner; }));
      }
    }
  }
  RC_CLASSIFY(winners > 0, "a winner was declared");
  RC_CLASSIFY(died, "a player died");
}

// How far apart the client and the server may end: Reconciliation leaves a
// divergence under a millimetre uncorrected (ADR-0004), and the two worlds
// step the same body in separate PhysX scenes.
constexpr float kConvergedPosition = 0.005F;
constexpr float kConvergedStamina = 0.001F;

// The client predicts every command while the server loses some of them,
// moving the player and handling its rifle for a lost one with no command, as
// it does when none arrives; each answer reaches the client delay ticks after
// the command it answers. Losses stop delay ticks before the end, so the
// client's newest answer, and every command it replays after it, followed what
// the server did.
RC_GTEST_PROP(SimulationPropertyTest, AClientThatDivergedConvergesOnTheServersState, ()) {
  augusta::parameters::Parameters parameters;
  parameters.stamina = *Stamina();
  parameters.rifle = *Rifle();
  parameters.ammo.max_range = 50.0F;
  const auto delay = *rc::gen::inRange<std::size_t>(1, 7);  // A round trip of up to 100 ms.
  const std::vector<Command> commands = *Commands(delay + 1);
  const auto lost = *rc::gen::container<std::set<std::size_t>>(rc::gen::inRange<std::size_t>(0, commands.size()));
  const std::size_t losses_end = commands.size() - delay - 1;

  augusta::simulation::World server(parameters, kTickRate);
  RC_ASSERT(server.AddCollisionMesh(Floor()).has_value());
  server.AddPlayer(kPlayer, kSpawn, kCharacter);

  augusta::prediction::World client;
  RC_ASSERT(client.AddCollisionMesh(Floor()).has_value());
  client.Start(kSpawn, parameters);

  std::vector<Acknowledgement> answers;  // answers[i] is the server's to commands[i].
  augusta::prediction::State predicted{};
  for (std::size_t i = 0; i < commands.size(); ++i) {
    const auto sequence = static_cast<augusta::command::Sequence>(i + 1);
    std::vector<augusta::simulation::PlayerCommand> received;
    if (i >= losses_end || !lost.contains(i)) {
      received.push_back({.entity = kPlayer, .command = commands[i]});
    }
    const augusta::simulation::EntityState answered = server.Tick(received, kTick).state.bodies.front();
    answers.push_back({.sequence = sequence, .body = answered.body, .rifle = answered.rifle});

    std::optional<Acknowledgement> answer;
    if (i >= delay) {
      answer = answers[i - delay];
    }
    predicted = client.Tick(commands[i], sequence, answer, kTick);
  }

  const BodyState& body = predicted.local_body;
  const BodyState& authoritative = answers.back().body;
  RC_ASSERT(augusta::math::Length(body.position - authoritative.position) < kConvergedPosition);
  RC_ASSERT(std::abs(body.stamina - authoritative.stamina) < kConvergedStamina);
  RC_ASSERT(body.stance == authoritative.stance);
  RC_ASSERT(body.exhausted == authoritative.exhausted);
  // The rifle is plain arithmetic, the same on both sides: it converges exactly.
  RC_ASSERT(predicted.rifle == answers.back().rifle);
  RC_CLASSIFY(predicted.rifle_corrections > 0, "the rifle was corrected");
}

}  // namespace
