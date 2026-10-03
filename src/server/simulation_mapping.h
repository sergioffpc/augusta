#ifndef AUGUSTA_SERVER_SIMULATION_MAPPING_H_
#define AUGUSTA_SERVER_SIMULATION_MAPPING_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"
#include "content.h"
#include "match.h"

// The conversions Host makes at SimulationWorld's edge, the way wire.h makes
// them at the protocol's: Match's names for players and bodies into
// SimulationWorld's, which are the same numbers, and the pack's characters into
// what SimulationWorld judges hits against.
namespace augusta::server {

/// entity as SimulationWorld names the same body.
[[nodiscard]] simulation::EntityId ToSimulation(EntityId entity);

/// A SimulationWorld body's entity as Match named it; the inverse of ToSimulation.
[[nodiscard]] EntityId FromSimulation(simulation::EntityId entity);

/// session as SimulationWorld's Game policy names the same player.
[[nodiscard]] simulation::SessionId ToSimulation(SessionId session);

/// A Game policy player's session as Match named it; the inverse of ToSimulation.
[[nodiscard]] SessionId FromSimulation(simulation::SessionId session);

[[nodiscard]] ballistics::BodyPart ToBallistics(assets::BodyPart part);

/// hitbox, of the character at path, as the triangles a bullet is tested
/// against. Throws std::runtime_error, naming the character, if its mesh is not
/// a whole, in-range triangle list: a pack's mesh blob is not checked for that
/// when it is decoded.
[[nodiscard]] simulation::CharacterHitbox ToSimulation(const assets::HitboxData& hitbox, const std::string& path);

/// Each of characters as SimulationWorld takes it, by its path. Throws as the
/// hitbox overload does.
[[nodiscard]] std::unordered_map<std::string, simulation::Character> ToSimulation(
    const std::vector<Character>& characters);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_SIMULATION_MAPPING_H_
