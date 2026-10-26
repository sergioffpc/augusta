#ifndef AUGUSTA_TESTS_CHARACTER_FIXTURE_H_
#define AUGUSTA_TESTS_CHARACTER_FIXTURE_H_

#include <cstdint>
#include <optional>

#include "augusta/simulation.h"
#include "clips_fixture.h"

// A test's simulation::Character whose own Clips do not matter to it, given
// some: every Character has Clips and a weapon socket (ADR-0066), as loading
// one refuses any without.
namespace augusta::character_fixture {

// character rigged to hold the rifle (clips_fixture::Rig): its Skeleton gains
// two arms and its Clips hold it still in its Rest pose, so each of its Hitboxes
// is where its Joint holds it at rest. Its weapon socket is on its Joint
// right_hand, the right arm's hand unless given, and holds the rifle's Grip at
// its standing Eye, the rifle level along its facing: a rifle whose Muzzle is
// on its Grip, as the default weapon::Markers' is, fires from its standing Eye
// along its view (ADR-0064).
inline simulation::Character Rigged(simulation::Character character,
                                    std::optional<std::uint32_t> right_hand = std::nullopt) {
  character.clips = clips_fixture::Rig(character.skeleton, right_hand, character.eye);
  return character;
}

}  // namespace augusta::character_fixture

#endif  // AUGUSTA_TESTS_CHARACTER_FIXTURE_H_
