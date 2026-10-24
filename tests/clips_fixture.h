#ifndef AUGUSTA_TESTS_CLIPS_FIXTURE_H_
#define AUGUSTA_TESTS_CLIPS_FIXTURE_H_

#include <cstddef>
#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/base/maths/transform.h>

#include "augusta/assets.h"
#include "augusta/clips.h"
#include "augusta/physics.h"

// A Character's Clips authored in memory for a test (ADR-0066), with
// ozz-animation's offline libraries: a Skeleton of its root Joint alone and
// still Clips, each locomotion Clip with the stride length and duration the test
// gives it, so a test never depends on the shipped Character's art.
namespace augusta::clips_fixture {

// A still Clip duration seconds long, one track for the root Joint, which a
// cooker would have measured stride_length meters of stride from.
inline animation::Clip StillClip(float duration, float stride_length = 0.0F) {
  ozz::animation::offline::RawAnimation raw;
  raw.duration = duration;
  raw.tracks.resize(1);
  auto built = ozz::animation::offline::AnimationBuilder()(raw);
  EXPECT_TRUE(built);
  animation::Clip clip{.animation = std::move(*built), .stride_length = stride_length, .foot_contacts = {}};
  if (stride_length > 0.0F) {
    clip.foot_contacts = {{.foot = assets::Foot::kLeft, .ratio = 0.0F}, {.foot = assets::Foot::kRight, .ratio = 0.5F}};
  }
  return clip;
}

// A locomotion Clip one second long that covers stride_length meters a cycle:
// its speed is stride_length meters a second.
inline animation::Clip Locomotion(float stride_length) { return StillClip(1.0F, stride_length); }

// A stance's Clips with these locomotion stride lengths, one second each, and
// still poses for the rest; no diagonals, no sprint, no sides when sides is false.
inline animation::StanceClips StanceOf(float forward, float backward, float left, float right, bool sides = true) {
  animation::StanceClips clips;
  clips.idle = StillClip(1.0F);
  clips.forward = Locomotion(forward);
  clips.backward = Locomotion(backward);
  clips.aim_down = StillClip(1.0F);
  clips.aim_center = StillClip(1.0F);
  clips.aim_up = StillClip(1.0F);
  clips.ads = StillClip(1.0F);
  if (sides) {
    clips.left = Locomotion(left);
    clips.right = Locomotion(right);
  }
  return clips;
}

// Standing locomotion strides, one second each, so each Clip moves as fast as
// its stride: forward at Movement's walk (3 m/s), sprint at its sprint (4.8 m/s).
inline constexpr float kStandingForward = 3.0F;
inline constexpr float kStandingBackward = 2.0F;
inline constexpr float kStandingLeft = 2.2F;
inline constexpr float kStandingRight = 2.4F;
inline constexpr float kStandingSprint = 4.8F;
inline constexpr float kCrouchingForward = 1.5F;
inline constexpr float kProneForward = 0.6F;
inline constexpr float kProneBackward = 0.5F;

// A Character's Clips: each stance's, with the strides above, standing's sprint,
// prone with no sides, and its air Clip.
inline std::shared_ptr<const animation::CharacterClips> TestClips() {
  ozz::animation::offline::RawSkeleton raw;
  raw.roots.resize(1);
  raw.roots[0].name = "Root";
  raw.roots[0].transform = ozz::math::Transform::identity();
  auto skeleton = ozz::animation::offline::SkeletonBuilder()(raw);
  EXPECT_TRUE(skeleton);

  auto clips = std::make_shared<animation::CharacterClips>();
  clips->skeleton = std::move(*skeleton);
  clips->joints = {0};
  animation::StanceClips standing = StanceOf(kStandingForward, kStandingBackward, kStandingLeft, kStandingRight);
  standing.sprint = Locomotion(kStandingSprint);
  clips->stances[static_cast<std::size_t>(physics::Stance::kStanding)] = std::move(standing);
  clips->stances[static_cast<std::size_t>(physics::Stance::kCrouching)] = StanceOf(kCrouchingForward, 1.2F, 1.3F, 1.3F);
  clips->stances[static_cast<std::size_t>(physics::Stance::kProne)] =
      StanceOf(kProneForward, kProneBackward, 0.0F, 0.0F, false);
  clips->air = StillClip(1.0F);
  clips->transitions = {.standing_crouching = 0.25F, .standing_prone = 0.8F, .crouching_prone = 0.6F};
  return clips;
}

}  // namespace augusta::clips_fixture

#endif  // AUGUSTA_TESTS_CLIPS_FIXTURE_H_
