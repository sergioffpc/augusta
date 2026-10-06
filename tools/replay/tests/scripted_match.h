#ifndef AUGUSTA_REPLAY_TESTS_SCRIPTED_MATCH_H_
#define AUGUSTA_REPLAY_TESTS_SCRIPTED_MATCH_H_

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <ios>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/math.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "content.h"
#include "recording.h"
#include "simulation_mapping.h"

// The example scenario as the server loads it from its golden server pack, and
// a scripted match recorded on it (ADR-0048): what the replay tests run on.
// tests/recording_test.cpp has its own copy, so the runtime's tests and the
// tools' stay independent. Needs AUGUSTA_EXAMPLE_PACKS, the directory of the
// golden packs.
namespace augusta::scripted_match {

inline constexpr std::uint8_t kTickRate = 60;
inline constexpr float kDeltaTime = 1.0F / kTickRate;

// Long enough for the scripted shooter to kill, at any fire rate the example's Parameters give.
inline constexpr int kMaxMatchTicks = 600;

// The two players of the scripted match, sessions 1 and 2.
inline constexpr simulation::EntityId kFirst{1};
inline constexpr simulation::EntityId kSecond{2};

inline assets::Pack LoadExamplePack() {
  const std::filesystem::path packs{AUGUSTA_EXAMPLE_PACKS};
  auto pack = assets::LoadVerifiedPack(packs / "server.pack", packs / "test.pub");
  if (!pack.has_value()) {
    throw std::runtime_error("the example server pack does not load");
  }
  return *std::move(pack);
}

inline server::Content LoadExampleContent() {
  auto content = server::LoadServerContent(LoadExamplePack(), kTickRate);
  if (!content.has_value()) {
    throw std::runtime_error("the example server pack's content does not load");
  }
  return *std::move(content);
}

inline server::RecordingHeader ExampleHeader() {
  return server::RecordingHeader{.engine_version = std::string(EngineVersion()),
                                 .server_pack = LoadExamplePack().Hash(),
                                 .tick_rate_hz = kTickRate};
}

// A World on the example's content, recording to recorder if it is given one.
inline server::RecordedSimulation ExampleSimulation(server::Content content, std::optional<server::Recorder> recorder) {
  return server::RecordedSimulation(
      server::BuildSimulation(content.parameters, kTickRate, content.scenario, std::move(content.policy)),
      std::move(recorder));
}

inline std::vector<simulation::MatchPlayer> ExampleEntrants(const server::Content& content) {
  const auto characters = server::ToSimulation(content.scenario.characters);
  const std::string& path = content.scenario.characters.front().path;
  return {
      simulation::MatchPlayer{.entity = kFirst,
                              .identity = {.session = simulation::SessionId{1}, .character = path},
                              .character = characters.at(path)},
      simulation::MatchPlayer{.entity = kSecond,
                              .identity = {.session = simulation::SessionId{2}, .character = path},
                              .character = characters.at(path)},
  };
}

// The view, on the angle grid, that looks from from to to.
inline command::Command Aiming(const math::Vec3& from, const math::Vec3& to) {
  const math::Vec3 along = to - from;
  command::Command command;
  command.yaw = math::SnapAngle(std::atan2(-along.x, -along.z));
  command.pitch = math::SnapAngle(std::atan2(along.y, std::hypot(along.x, along.z)));
  return command;
}

// The scripted match, a duel: each player stands, looks at the other's chest
// and taps its trigger every tenth tick, until they kill each other on the same
// tick and Game policy ends the Match as a Draw; then the Match is ended in the
// world, as the server ends it after the tick, and one more tick runs. Returns
// every tick's result.
inline std::vector<simulation::TickResult> PlayScriptedMatch(server::RecordedSimulation& simulation,
                                                             const server::Content& content) {
  constexpr int kTapEvery = 10;
  const std::vector<math::Vec3> spawns = simulation.StartMatch(ExampleEntrants(content), content.scenario.spawn_points);
  const math::Vec3 eye = content.scenario.characters.front().eye;
  const math::Vec3 chest = eye * 0.75F;
  const command::Command first_aim = Aiming(spawns[0] + eye, spawns[1] + chest);
  const command::Command second_aim = Aiming(spawns[1] + eye, spawns[0] + chest);
  std::vector<simulation::TickResult> results;
  tick::Tick last_tick = 0;
  for (int i = 0; i < kMaxMatchTicks; ++i) {
    command::Command first = first_aim;
    first.fire = i % kTapEvery == 0;
    first.seen_tick = last_tick;
    command::Command second = second_aim;
    second.fire = first.fire;
    second.seen_tick = last_tick;
    results.push_back(simulation.Tick({simulation::PlayerCommand{.entity = kFirst, .command = first},
                                       simulation::PlayerCommand{.entity = kSecond, .command = second}},
                                      kDeltaTime));
    last_tick = results.back().state.tick;
    if (!results.back().actions.empty()) {
      break;
    }
  }
  simulation.EndMatch();
  results.push_back(simulation.Tick({}, kDeltaTime));
  return results;
}

// The scripted match, recorded.
inline std::string RecordScriptedMatch() {
  std::ostringstream out(std::ios::binary);
  const server::Content content = LoadExampleContent();
  server::RecordedSimulation simulation =
      ExampleSimulation(LoadExampleContent(), server::Recorder(out, ExampleHeader()));
  PlayScriptedMatch(simulation, content);
  return std::move(out).str();
}

// Both players walking the same way for a second, recorded: bodies that move
// on every tick.
inline std::string RecordAWalk() {
  constexpr int kWalkTicks = 60;
  std::ostringstream out(std::ios::binary);
  const server::Content content = LoadExampleContent();
  server::RecordedSimulation simulation =
      ExampleSimulation(LoadExampleContent(), server::Recorder(out, ExampleHeader()));
  simulation.StartMatch(ExampleEntrants(content), content.scenario.spawn_points);
  command::Command walk;
  walk.movement.direction = math::Vec3(1.0F, 0.0F, 0.0F);
  for (int i = 0; i < kWalkTicks; ++i) {
    simulation.Tick({simulation::PlayerCommand{.entity = kFirst, .command = walk},
                     simulation::PlayerCommand{.entity = kSecond, .command = walk}},
                    kDeltaTime);
  }
  return std::move(out).str();
}

inline server::Recording Read(const std::string& bytes) {
  std::istringstream in(bytes, std::ios::binary);
  auto recording = server::ReadRecording(in);
  EXPECT_TRUE(recording.has_value());
  return recording.value_or(server::Recording{});
}

}  // namespace augusta::scripted_match

#endif  // AUGUSTA_REPLAY_TESTS_SCRIPTED_MATCH_H_
