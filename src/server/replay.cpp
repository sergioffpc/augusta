#include "replay.h"

#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "augusta/math.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "content.h"
#include "recording.h"
#include "simulation_mapping.h"

namespace augusta::server {

namespace {

bool Near(const math::Vec3& recorded, const math::Vec3& replayed, float tolerance) {
  return std::abs(recorded.x - replayed.x) <= tolerance && std::abs(recorded.y - replayed.y) <= tolerance &&
         std::abs(recorded.z - replayed.z) <= tolerance;
}

// Whether every element of recorded is the same as replayed's of the same index, by same.
template <typename Element, typename Same>
bool AllSame(const std::vector<Element>& recorded, const std::vector<Element>& replayed, Same same) {
  if (recorded.size() != replayed.size()) {
    return false;
  }
  for (std::size_t i = 0; i < recorded.size(); ++i) {
    if (!same(recorded[i], replayed[i])) {
      return false;
    }
  }
  return true;
}

// Whether replayed is recorded, its position and velocity within their
// tolerances and every other field equal: compared whole, so a field
// EntityState gains is compared too.
bool SameBody(const simulation::EntityState& recorded, simulation::EntityState replayed, float position,
              float velocity) {
  if (!Near(recorded.body.position, replayed.body.position, position) ||
      !Near(recorded.body.velocity, replayed.body.velocity, velocity)) {
    return false;
  }
  replayed.body.position = recorded.body.position;
  replayed.body.velocity = recorded.body.velocity;
  return replayed == recorded;
}

// Whether replayed is recorded, its origin within position and every other field equal.
bool SameShot(const simulation::Shot& recorded, simulation::Shot replayed, float position) {
  if (!Near(recorded.origin, replayed.origin, position)) {
    return false;
  }
  replayed.origin = recorded.origin;
  return replayed == recorded;
}

// The players of a recorded Match start as the World takes them, their
// Characters from characters; nullopt if one names a Character it lacks.
std::optional<std::vector<simulation::MatchPlayer>> Entrants(
    const std::vector<RecordedEntrant>& recorded,
    const std::unordered_map<std::string, simulation::Character>& characters) {
  std::vector<simulation::MatchPlayer> players;
  players.reserve(recorded.size());
  for (const RecordedEntrant& entrant : recorded) {
    const auto character = characters.find(entrant.identity.character);
    if (character == characters.end()) {
      return std::nullopt;
    }
    players.push_back(simulation::MatchPlayer{
        .entity = entrant.entity, .identity = entrant.identity, .character = character->second});
  }
  return players;
}

// Puts every body of world where recorded has it, so a grid step one build
// rounds differently from another is not carried into the next tick, where it
// could grow: each tick of a replay across builds starts from the recorded one.
void Resync(simulation::World& world, const TickOutcome& recorded) {
  for (const simulation::EntityState& body : recorded.bodies) {
    world.PlaceBody(body.entity, body.body);
  }
}

}  // namespace

std::string_view DescribeDivergenceKind(DivergenceKind kind) {
  switch (kind) {
    case DivergenceKind::kUnknownCharacter:
      return "a Match start names a character the content lacks";
    case DivergenceKind::kTick:
      return "the World numbered the tick otherwise";
    case DivergenceKind::kSpawns:
      return "the players spawned elsewhere";
    case DivergenceKind::kBodies:
      return "a body differs";
    case DivergenceKind::kShots:
      return "the rounds fired differ";
    case DivergenceKind::kHits:
      return "the hits differ";
    case DivergenceKind::kDeaths:
      return "the deaths differ";
    case DivergenceKind::kMatchEnd:
      return "Game policy's Match end differs";
  }
  return "unknown divergence";
}

std::optional<DivergenceKind> FindDivergence(const TickOutcome& recorded, const TickOutcome& replayed, float delta_time,
                                             const Tolerance& tolerance) {
  const float position = tolerance.position;
  // Both ends of the tick's displacement may be off.
  const float velocity = delta_time > 0.0F ? 2.0F * position / delta_time : 0.0F;
  if (recorded.tick != replayed.tick) {
    return DivergenceKind::kTick;
  }
  if (!AllSame(recorded.spawns, replayed.spawns,
               [&](const math::Vec3& a, const math::Vec3& b) { return Near(a, b, position); })) {
    return DivergenceKind::kSpawns;
  }
  if (!AllSame(recorded.bodies, replayed.bodies,
               [&](const auto& a, const auto& b) { return SameBody(a, b, position, velocity); })) {
    return DivergenceKind::kBodies;
  }
  if (!AllSame(recorded.shots, replayed.shots,
               [&](const auto& a, const auto& b) { return SameShot(a, b, position); })) {
    return DivergenceKind::kShots;
  }
  if (recorded.hits != replayed.hits) {
    return DivergenceKind::kHits;
  }
  if (recorded.deaths != replayed.deaths) {
    return DivergenceKind::kDeaths;
  }
  if (recorded.match_end != replayed.match_end) {
    return DivergenceKind::kMatchEnd;
  }
  return std::nullopt;
}

std::expected<tick::Tick, Divergence> Replay(const Recording& recording, Content content, const Tolerance& tolerance) {
  const std::unordered_map<std::string, simulation::Character> characters = ToSimulation(content.scenario.characters);
  simulation::World world =
      BuildSimulation(content.parameters, recording.header.tick_rate_hz, content.scenario, std::move(content.policy));
  for (const TickRecord& record : recording.ticks) {
    const TickInput& input = record.input;
    // In the order RecordedSimulation hands them to the World it records.
    if (input.match_ended) {
      world.EndMatch();
    }
    for (const simulation::EntityId entity : input.removed) {
      world.RemovePlayer(entity);
    }
    std::vector<math::Vec3> spawns;
    if (!input.match_start.empty()) {
      const auto players = Entrants(input.match_start, characters);
      if (!players.has_value()) {
        return std::unexpected(Divergence{.tick = record.outcome.tick,
                                          .kind = DivergenceKind::kUnknownCharacter,
                                          .recorded = record.outcome,
                                          .replayed = {}});
      }
      spawns = world.StartMatch(*players, content.scenario.spawn_points);
    }
    TickOutcome replayed = OutcomeOf(spawns, world.Tick(input.commands, input.delta_time));
    if (const auto kind = FindDivergence(record.outcome, replayed, input.delta_time, tolerance); kind.has_value()) {
      return std::unexpected(Divergence{
          .tick = record.outcome.tick, .kind = *kind, .recorded = record.outcome, .replayed = std::move(replayed)});
    }
    if (tolerance.position > 0.0F) {
      Resync(world, record.outcome);
    }
  }
  return static_cast<tick::Tick>(recording.ticks.size());
}

}  // namespace augusta::server
