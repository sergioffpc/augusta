#include "simulation_mapping.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/policy_actions.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "content.h"
#include "match.h"

namespace augusta::server {

simulation::EntityId ToSimulation(EntityId entity) {
  return static_cast<simulation::EntityId>(static_cast<std::uint32_t>(entity));
}

EntityId FromSimulation(simulation::EntityId entity) {
  return static_cast<EntityId>(static_cast<std::uint32_t>(entity));
}

simulation::SessionId ToSimulation(SessionId session) {
  return static_cast<simulation::SessionId>(std::to_underlying(session));
}

SessionId FromSimulation(simulation::SessionId session) { return static_cast<SessionId>(std::to_underlying(session)); }

ballistics::BodyPart ToBallistics(assets::BodyPart part) {
  switch (part) {
    case assets::BodyPart::kHead:
      return ballistics::BodyPart::kHead;
    case assets::BodyPart::kTorso:
      return ballistics::BodyPart::kTorso;
    case assets::BodyPart::kLimb:
      return ballistics::BodyPart::kLimb;
  }
  std::unreachable();
}

simulation::CharacterHitbox ToSimulation(const assets::HitboxData& hitbox, const std::string& path) {
  const assets::MeshData& mesh = hitbox.mesh;
  if (const auto valid = physics::ValidateCollisionMesh({.points = mesh.points, .indices = mesh.indices}); !valid) {
    throw std::runtime_error(std::format("server::Host: hitbox of character {} rejected: {}", path,
                                         physics::DescribeCollisionMeshError(valid.error())));
  }
  simulation::CharacterHitbox result{.part = ToBallistics(hitbox.part), .triangles = {}};
  result.triangles.reserve(mesh.indices.size() / 3);
  for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
    result.triangles.push_back(ballistics::Triangle{.a = mesh.points[mesh.indices[i]],
                                                    .b = mesh.points[mesh.indices[i + 1]],
                                                    .c = mesh.points[mesh.indices[i + 2]]});
  }
  return result;
}

std::unordered_map<std::string, simulation::Character> ToSimulation(const std::vector<Character>& characters) {
  std::unordered_map<std::string, simulation::Character> result;
  result.reserve(characters.size());
  for (const Character& character : characters) {
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
                                  const Scenario& scenario, scripting::Engine policy) {
  simulation::World simulation(parameters, tick_rate_hz, std::move(policy));
  for (const physics::CollisionMesh& mesh : scenario.collision) {
    if (const auto added = simulation.AddCollisionMesh(mesh); !added) {
      throw std::runtime_error(
          std::format("server::Host: map collision rejected: {}", physics::DescribeCollisionMeshError(added.error())));
    }
  }
  return simulation;
}

}  // namespace augusta::server
