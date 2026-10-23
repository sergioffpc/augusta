#ifndef AUGUSTA_SHOWN_HITBOXES_H_
#define AUGUSTA_SHOWN_HITBOXES_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "augusta/debug_shapes.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"
#include "augusta/presentation.h"

/// \file
/// Other players' Hitboxes where a frame shows them, for the Physics Debug
/// overlays (ADR-0075): placed by gameplay's hitbox::Place, the function the
/// server's Lag compensation tests a round against (ADR-0044), and outlined as
/// Debug overlay shapes. The Hitboxes overlay draws them each frame; Shot
/// judgements keeps them as a round's seen Hitboxes. Compiled only in a
/// Development build (ADR-0074).
namespace augusta::presentation {

/// Each character's Hitboxes, by its path (ADR-0042), as the client loaded them.
using CharacterHitboxes = std::unordered_map<std::string, std::vector<hitbox::CharacterHitbox>>;

/// Every Hitbox of every other player at its pose in state
/// (state.remote_players), player by player; none of a player whose character
/// hitboxes has none of.
[[nodiscard]] std::vector<hitbox::PlacedHitbox> PlaceShownPlayers(const CharacterHitboxes& hitboxes,
                                                                  const State& state);

/// Appends the three edges of each of hitbox's triangles to shapes, as
/// opaque segments in color.
void AddOutline(const hitbox::PlacedHitbox& hitbox, const math::Vec3& color, bool through_walls, debug::Shapes& shapes);

}  // namespace augusta::presentation

#endif  // AUGUSTA_SHOWN_HITBOXES_H_
