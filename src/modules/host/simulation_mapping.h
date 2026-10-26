#ifndef AUGUSTA_HOST_SIMULATION_MAPPING_H_
#define AUGUSTA_HOST_SIMULATION_MAPPING_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "augusta/assets.h"
#include "augusta/content.h"
#include "augusta/hitbox.h"
#include "augusta/match.h"
#include "augusta/parameters.h"
#include "augusta/policy.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"

/// \file
/// The conversions Host makes at SimulationWorld's edge, the way
/// link_mapping.h makes them at the Link's: Match's names for players
/// and bodies into SimulationWorld's, which are the same numbers, and the pack's
/// characters into what SimulationWorld judges hits against; and the World
/// itself, built from the scenario.
namespace augusta::host {

/// entity as SimulationWorld names the same body.
[[nodiscard]] simulation::EntityId ToSimulation(EntityId entity);

/// A SimulationWorld body's entity as Match named it; the inverse of ToSimulation.
[[nodiscard]] EntityId FromSimulation(simulation::EntityId entity);

/// hitbox, of the character at path, as the triangles a bullet is tested
/// against, converted as the client converts it (hitbox::FromPack). Throws
/// failure::ClassifiedFailure (failure::Code::kInvalidContent), naming the
/// character, if its mesh is not a whole, in-range triangle list.
[[nodiscard]] hitbox::CharacterHitbox ToSimulation(const assets::HitboxData& hitbox, const std::string& path);

/// hitbox as the HitboxData overload converts it, bound to the Joint of its
/// character's Skeleton it names. Throws as that overload does.
[[nodiscard]] hitbox::CharacterHitbox ToSimulation(const assets::JointHitbox& hitbox, const std::string& path);

/// Each of characters as SimulationWorld takes it, by its path: its Skeleton,
/// and its hitboxes each in its Joint's space, for SimulationWorld's Posing
/// phase to pose (ADR-0061), and its Clips, shared, for its Movement to advance
/// each Stride by (ADR-0066). Throws as the hitbox overloads do.
[[nodiscard]] std::unordered_map<std::string, simulation::Character> ToSimulation(
    const std::vector<scenario::Character>& characters);

/// SimulationWorld on parameters at tick_rate_hz, with scenario's collision, its
/// rifle's markers and the scenario's Game policy. Throws failure::ClassifiedFailure
/// (failure::Code::kInvalidContent) if a map mesh is not a whole triangle list.
[[nodiscard]] simulation::World BuildSimulation(const parameters::Parameters& parameters, std::uint8_t tick_rate_hz,
                                                const scenario::Scenario& scenario, policy::Policy policy);

}  // namespace augusta::host

#endif  // AUGUSTA_HOST_SIMULATION_MAPPING_H_
