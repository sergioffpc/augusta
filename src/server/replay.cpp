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
#include "augusta/policy_actions.h"
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

bool SameBody(const simulation::EntityState& recorded, const simulation::EntityState& replayed, float position,
              float velocity) {
  return recorded.entity == replayed.entity && Near(recorded.body.position, replayed.body.position, position) &&
         Near(recorded.body.velocity, replayed.body.velocity, velocity) &&
         recorded.body.stance == replayed.body.stance && recorded.body.stamina == replayed.body.stamina &&
         recorded.body.exhausted == replayed.body.exhausted && recorded.yaw == replayed.yaw &&
         recorded.health == replayed.health && recorded.rifle == replayed.rifle;
}

bool SameShot(const simulation::Shot& recorded, const simulation::Shot& replayed, float position) {
  return recorded.shooter == replayed.shooter && Near(recorded.origin, replayed.origin, position) &&
         recorded.yaw == replayed.yaw && recorded.pitch == replayed.pitch;
}

bool SameHit(const simulation::Hit& recorded, const simulation::Hit& replayed) {
  return recorded.shooter == replayed.shooter && recorded.target == replayed.target &&
         recorded.damage == replayed.damage && recorded.health == replayed.health && recorded.part == replayed.part &&
         recorded.reached_zero == replayed.reached_zero;
}

bool SameDeath(const simulation::Death& recorded, const simulation::Death& replayed) {
  return recorded.victim == replayed.victim && recorded.killer == replayed.killer && recorded.yaw == replayed.yaw &&
         recorded.pitch == replayed.pitch && recorded.part == replayed.part;
}

bool SameMatchEnd(const std::optional<simulation::MatchEnd>& recorded,
                  const std::optional<simulation::MatchEnd>& replayed) {
  if (recorded.has_value() != replayed.has_value()) {
    return false;
  }
  return !recorded.has_value() || recorded->winner == replayed->winner;
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

}  // namespace

std::string_view DescribeDivergenceKind(DivergenceKind kind) {
  switch (kind) {
    case DivergenceKind::kUnknownCharacter:
      return "a Match start names a character the content lacks";
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
  if (!AllSame(recorded.hits, replayed.hits, SameHit)) {
    return DivergenceKind::kHits;
  }
  if (!AllSame(recorded.deaths, replayed.deaths, SameDeath)) {
    return DivergenceKind::kDeaths;
  }
  if (!SameMatchEnd(recorded.match_end, replayed.match_end)) {
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
  }
  return static_cast<tick::Tick>(recording.ticks.size());
}

}  // namespace augusta::server
