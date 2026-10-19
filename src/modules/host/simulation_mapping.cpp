#include "augusta/simulation_mapping.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/content.h"
#include "augusta/failure.h"
#include "augusta/map.h"
#include "augusta/match.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/policy.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"

namespace augusta::host {

simulation::EntityId ToSimulation(EntityId entity) {
  return static_cast<simulation::EntityId>(static_cast<std::uint32_t>(entity));
}

EntityId FromSimulation(simulation::EntityId entity) {
  return static_cast<EntityId>(static_cast<std::uint32_t>(entity));
}

policy::SessionId ToSimulation(SessionId session) {
  return static_cast<policy::SessionId>(std::to_underlying(session));
}

SessionId FromSimulation(policy::SessionId session) { return static_cast<SessionId>(std::to_underlying(session)); }

body_part::BodyPart ToSimulation(assets::BodyPart part) {
  switch (part) {
    case assets::BodyPart::kHead:
      return body_part::BodyPart::kHead;
    case assets::BodyPart::kTorso:
      return body_part::BodyPart::kTorso;
    case assets::BodyPart::kLimb:
      return body_part::BodyPart::kLimb;
  }
  std::unreachable();
}

simulation::CharacterHitbox ToSimulation(const assets::HitboxData& hitbox, const std::string& path) {
  const assets::MeshData& mesh = hitbox.mesh;
  if (const auto valid = physics::ValidateCollisionMesh({.points = mesh.points, .indices = mesh.indices}); !valid) {
    throw failure::ClassifiedFailure(failure::Failure{
        .code = failure::Code::kInvalidContent,
        .context = {{.key = "character", .value = path}},
        .detail = std::format("hitbox rejected: {}", physics::DescribeCollisionMeshError(valid.error()))});
  }
  simulation::CharacterHitbox result{.part = ToSimulation(hitbox.part), .triangles = {}};
  result.triangles.reserve(mesh.indices.size() / 3);
  for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
    result.triangles.push_back(ballistics::Triangle{.a = mesh.points[mesh.indices[i]],
                                                    .b = mesh.points[mesh.indices[i + 1]],
                                                    .c = mesh.points[mesh.indices[i + 2]]});
  }
  return result;
}

std::unordered_map<std::string, simulation::Character> ToSimulation(
    const std::vector<scenario::Character>& characters) {
  std::unordered_map<std::string, simulation::Character> result;
  result.reserve(characters.size());
  for (const scenario::Character& character : characters) {
    simulation::Character converted{.eye = character.eye, .hitboxes = {}};
    converted.hitboxes.reserve(character.hitboxes.size());
    for (const assets::HitboxData& hitbox : character.hitboxes) {
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
