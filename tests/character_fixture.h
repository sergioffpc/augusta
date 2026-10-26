#ifndef AUGUSTA_TESTS_CHARACTER_FIXTURE_H_
#define AUGUSTA_TESTS_CHARACTER_FIXTURE_H_

#include <cstdint>

#include "augusta/simulation.h"
#include "clips_fixture.h"

// A test's simulation::Character whose own Clips do not matter to it, given
// some: every Character has Clips and a weapon socket (ADR-0066), as loading
// one refuses any without.
namespace augusta::character_fixture {

// character rigged to hold the rifle (clips_fixture::Rig): its Skeleton gains a
// left arm and its Clips hold it still in its Rest pose, so each of its Hitboxes
// is where its Joint holds it at rest. Its weapon socket is on its Joint
// right_hand.
inline simulation::Character Rigged(simulation::Character character, std::uint32_t right_hand = 0) {
  character.clips = clips_fixture::Rig(character.skeleton, right_hand);
  return character;
}

}  // namespace augusta::character_fixture

#endif  // AUGUSTA_TESTS_CHARACTER_FIXTURE_H_
