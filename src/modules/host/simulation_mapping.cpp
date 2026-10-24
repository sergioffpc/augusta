#include "augusta/simulation_mapping.h"

#include <cstdint>
#include <expected>
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

namespace {

// converted unwrapped: a Hitbox of the character at path as hitbox::FromPack
// converts it, or, at startup, the failure naming that character thrown if it
// was refused (ADR-0033).
hitbox::CharacterHitbox UnwrapHitboxOrThrow(
    std::expected<hitbox::CharacterHitbox, physics::CollisionMeshError> converted, const std::string& path) {
  if (!converted) {
    throw failure::ClassifiedFailure(failure::Failure{
        .code = failure::Code::kInvalidContent,
        .context = {{.key = "character", .value = path}},
        .detail = std::format("hitbox rejected: {}", physics::DescribeCollisionMeshError(converted.error()))});
  }
  return *std::move(converted);
}

}  // namespace

hitbox::CharacterHitbox ToSimulation(const assets::HitboxData& hitbox, const std::string& path) {
  return UnwrapHitboxOrThrow(hitbox::FromPack(hitbox), path);
}

hitbox::CharacterHitbox ToSimulation(const assets::JointHitbox& hitbox, const std::string& path) {
  return UnwrapHitboxOrThrow(hitbox::FromPack(hitbox), path);
}

std::unordered_map<std::string, simulation::Character> ToSimulation(
    const std::vector<scenario::Character>& characters) {
  std::unordered_map<std::string, simulation::Character> result;
  result.reserve(characters.size());
  for (const scenario::Character& character : characters) {
    simulation::Character converted{.eye = character.eye, .hitboxes = {}, .skeleton = character.skeleton};
    converted.hitboxes.reserve(character.hitboxes.size());
    for (const assets::JointHitbox& hitbox : character.hitboxes) {
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
