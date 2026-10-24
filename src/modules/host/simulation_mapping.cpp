#include "augusta/simulation_mapping.h"

#include <cstdint>
#include <format>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/content.h"
#include "augusta/failure.h"
#include "augusta/hitbox.h"
#include "augusta/map.h"
#include "augusta/match.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/policy.h"
#include "augusta/simulation.h"

namespace augusta::host {

simulation::EntityId ToSimulation(EntityId entity) {
  return static_cast<simulation::EntityId>(static_cast<std::uint32_t>(entity));
}

EntityId FromSimulation(simulation::EntityId entity) {
  return static_cast<EntityId>(static_cast<std::uint32_t>(entity));
}

hitbox::CharacterHitbox ToSimulation(const assets::HitboxData& hitbox, const std::string& path) {
  auto converted = hitbox::FromPack(hitbox);
  if (!converted) {
    throw failure::ClassifiedFailure(failure::Failure{
        .code = failure::Code::kInvalidContent,
        .context = {{.key = "character", .value = path}},
        .detail = std::format("hitbox rejected: {}", physics::DescribeCollisionMeshError(converted.error()))});
  }
  return *std::move(converted);
}

std::unordered_map<std::string, simulation::Character> ToSimulation(
    const std::vector<scenario::Character>& characters) {
  std::unordered_map<std::string, simulation::Character> result;
  result.reserve(characters.size());
  for (const scenario::Character& character : characters) {
    simulation::Character converted{.eye = character.eye, .hitboxes = {}};
    converted.hitboxes.reserve(character.hitboxes.size());
    // Rigid until SimulationWorld poses a Skeleton itself (ADR-0061).
    for (const assets::HitboxData& hitbox : assets::PlaceHitboxesAtRest(character.skeleton, character.hitboxes)) {
      converted.hitboxes.push_back(ToSimulation(hitbox, character.path));
    }
    result.emplace(character.path, std::move(converted));
  }
  return result;
}

simulation::World BuildSimulation(const parameters::Parameters& parameters, std::uint8_t tick_rate_hz,
                                  const scenario::Scenario& scenario, policy::Policy policy) {
  simulation::World simulation(parameters, tick_rate_hz, std::move(policy));
  if (const auto added = map::AddCollision(simulation, scenario.collision); !added) {
    throw failure::ClassifiedFailure(failure::Failure{
        .code = failure::Code::kInvalidContent,
        .context = {},
        .detail = std::format("map collision rejected: {}", physics::DescribeCollisionMeshError(added.error()))});
  }
  return simulation;
}

}  // namespace augusta::host
