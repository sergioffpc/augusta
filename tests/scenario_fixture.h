#ifndef AUGUSTA_TESTS_SCENARIO_FIXTURE_H_
#define AUGUSTA_TESTS_SCENARIO_FIXTURE_H_

#include <optional>

#include "augusta/assets.h"
#include "augusta/content.h"
#include "augusta/math.h"
#include "clips_fixture.h"

// A test scenario's Character whose own Clips do not matter to it, given some:
// every Character has Clips and a weapon socket (ADR-0066), as loading one
// refuses any without.
namespace augusta::scenario_fixture {

// character rigged to hold the rifle (clips_fixture::Rig): its Skeleton, a root
// Joint alone if it has none, gains two arms, and its Clips hold it still in
// its Rest pose, so each of its Hitboxes is where its Joint holds it at rest.
// Its weapon socket holds the rifle's Grip at its standing Eye, the rifle level
// along its facing: a rifle whose Muzzle is on its Grip, as the default
// weapon::Markers' is, fires from its standing Eye along its view (ADR-0064).
inline scenario::Character Rigged(scenario::Character character) {
  if (character.skeleton.joints.empty()) {
    character.skeleton = {.joints = {assets::JointData{.name = "Root"}}, .rest_transforms = {math::Mat4(1.0F)}};
  }
  character.clips = clips_fixture::Rig(character.skeleton, std::nullopt, character.eye);
  return character;
}

}  // namespace augusta::scenario_fixture

#endif  // AUGUSTA_TESTS_SCENARIO_FIXTURE_H_
