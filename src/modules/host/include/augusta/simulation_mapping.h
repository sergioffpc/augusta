#ifndef AUGUSTA_SIMULATION_MAPPING_H_
#define AUGUSTA_SIMULATION_MAPPING_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "augusta/assets.h"
#include "augusta/body_part.h"
#include "augusta/content.h"
#include "augusta/match.h"
#include "augusta/parameters.h"
#include "augusta/policy.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"

/// \file
/// The conversions Host makes at SimulationWorld's edge, the way
/// augusta/link_mapping.h makes them at the Link's: Match's names for players
/// and bodies into SimulationWorld's, which are the same numbers, and the pack's
/// characters into what SimulationWorld judges hits against; and the World
/// itself, built from the scenario.
namespace augusta::host {

/// entity as SimulationWorld names the same body.
[[nodiscard]] simulation::EntityId ToSimulation(EntityId entity);

/// A SimulationWorld body's entity as Match named it; the inverse of ToSimulation.
[[nodiscard]] EntityId FromSimulation(simulation::EntityId entity);

/// session as SimulationWorld's Game policy names the same player.
[[nodiscard]] policy::SessionId ToSimulation(SessionId session);

/// A Game policy player's session as Match named it; the inverse of ToSimulation.
[[nodiscard]] SessionId FromSimulation(policy::SessionId session);

/// part, in the blob's numbering, as the Body part every module names, the way
/// the link's FromWire takes one off the protocol's wire enum.
[[nodiscard]] body_part::BodyPart FromBlob(assets::BodyPart part);

/// hitbox, of the character at path, as the triangles a bullet is tested
/// against. Throws failure::ClassifiedFailure (failure::Code::kInvalidContent),
/// naming the character, if its mesh is not a whole, in-range triangle list: a
/// pack's mesh blob is not checked for that when it is decoded.
[[nodiscard]] simulation::CharacterHitbox ToSimulation(const assets::HitboxData& hitbox, const std::string& path);

/// Each of characters as SimulationWorld takes it, by its path. Throws as the
/// hitbox overload does.
[[nodiscard]] std::unordered_map<std::string, simulation::Character> ToSimulation(
    const std::vector<scenario::Character>& characters);

/// SimulationWorld on parameters at tick_rate_hz, with scenario's collision and
/// the scenario's Game policy. Throws failure::ClassifiedFailure
/// (failure::Code::kInvalidContent) if a map mesh is not a whole triangle list.
[[nodiscard]] simulation::World BuildSimulation(const parameters::Parameters& parameters, std::uint8_t tick_rate_hz,
                                                const scenario::Scenario& scenario, policy::Policy policy);

}  // namespace augusta::host

#endif  // AUGUSTA_SIMULATION_MAPPING_H_
