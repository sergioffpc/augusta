#include "hitbox_history.h"

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include "augusta/animation.h"
#include "augusta/assets.h"
#include "augusta/body_part.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "hitbox_posing.h"

// The Hitbox history's cull (ADR-0061), at the simulation module's own seam:
// how far a character's hitboxes reach in any pose it keeps, and the root that
// is measured around, each against what At composes. Lag compensation through
// simulation::World is simulation_test.cpp's.
namespace {

using augusta::animation::JointPose;
using augusta::animation::SkeletonPose;
using augusta::assets::JointData;
using augusta::assets::Skeleton;
using augusta::hitbox::CharacterHitbox;
using augusta::hitbox::Pose;
using augusta::math::Mat4;
using augusta::math::Quat;
using augusta::math::Vec3;
using augusta::simulation::PoseHistory;
using augusta::simulation::Reach;

constexpr std::size_t kCapacity = 4;

// A Root, an Arm under it scaled 2 along X, and a Hand under the Arm.
Skeleton ArmSkeleton() {
  return Skeleton{
      .joints = {JointData{.name = "Root"}, JointData{.name = "Root/Arm", .parent = 0, .scale = Vec3(2.0F, 1.0F, 1.0F)},
                 JointData{.name = "Root/Arm/Hand", .parent = 1}},
      .rest_transforms = {Mat4(1.0F), Mat4(1.0F), Mat4(1.0F)}};
}

// A square on the Hand, 0.2 m across, 0.7 m out along its X: far enough out
// that how far the Arm's scale stretches it shows beyond the margin.
std::vector<CharacterHitbox> HandHitbox() {
  const Vec3 a(0.6F, -0.1F, 0.0F);
  const Vec3 b(0.8F, -0.1F, 0.0F);
  const Vec3 c(0.8F, 0.1F, 0.0F);
  const Vec3 d(0.6F, 0.1F, 0.0F);
  return {CharacterHitbox{.part = augusta::body_part::BodyPart::kLimb,
                          .triangles = {{.a = a, .b = b, .c = c}, {.a = a, .b = c, .c = d}},
                          .joint = 2}};
}

// ArmSkeleton posed with its root at x along X, facing yaw, the Arm reach out
// from its root along X, turned by turn about Z, and the Hand hand out along
// the Arm's X.
SkeletonPose Posed(float x, float yaw, float reach, float turn, float hand) {
  const Quat none(1.0F, 0.0F, 0.0F, 0.0F);
  return SkeletonPose{
      .root = Pose{.position = Vec3(x, 0.0F, 0.0F), .yaw = yaw, .stance = augusta::physics::Stance::kStanding},
      .local = {JointPose{.translation = Vec3(0.0F), .rotation = none},
                JointPose{.translation = Vec3(reach, 1.0F, 0.0F),
                          .rotation = glm::angleAxis(turn, Vec3(0.0F, 0.0F, 1.0F))},
                JointPose{.translation = Vec3(hand, 0.0F, 0.0F), .rotation = none}},
      .world = {}};
}

// The arm stretched out on tick 1 and drawn in on the ticks after: a cull by
// how far the newest pose reaches would drop a round that crosses the hand
// where tick 1 holds it. At every moment the history gives, the reach in any
// pose is no less than how far the pose At composes reaches.
// Requirements: US-11, NFR-02
TEST(PoseHistoryTest, TheReachInAnyPoseBoundsThePoseAtEveryMomentKept) {
  const std::vector<CharacterHitbox> hitboxes = HandHitbox();
  PoseHistory history(kCapacity, ArmSkeleton(), hitboxes);
  history.Record(1, Posed(0.0F, 0.0F, 1.5F, 0.0F, 1.0F));
  history.Record(2, Posed(0.5F, 1.0F, 0.2F, 1.2F, 0.1F));
  history.Record(3, Posed(1.0F, 2.0F, 0.1F, 3.0F, 0.1F));

  const Reach reach(hitboxes);
  std::vector<Mat4> joints;
  for (const double time : {1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0}) {
    const std::optional<Pose> pose = history.At(time, joints);
    ASSERT_TRUE(pose.has_value());
    EXPECT_LE(reach.Of(pose->position, joints), history.ReachInAnyPose()) << "at " << time;
  }
  // The control: at tick 1 the hand reaches farther than the newest pose's
  // whole reach, so a bound from the newest pose alone would not hold.
  std::optional<Pose> newest = history.At(3.0, joints);
  ASSERT_TRUE(newest.has_value());
  const float newest_reach = reach.Of(newest->position, joints);
  std::optional<Pose> oldest = history.At(1.0, joints);
  ASSERT_TRUE(oldest.has_value());
  EXPECT_GT(reach.Of(oldest->position, joints), newest_reach);
}

// RootAt gives the root At composes the Joints onto, between ticks and beyond
// them, without composing them: the root a round is culled around is the
// one its hitboxes are judged on.
// Requirements: US-11
TEST(PoseHistoryTest, RootAtIsTheRootAtGives) {
  PoseHistory history(kCapacity, ArmSkeleton(), HandHitbox());
  std::vector<Mat4> joints;
  EXPECT_FALSE(history.RootAt(1.0).has_value());

  history.Record(1, Posed(0.0F, 0.0F, 1.0F, 0.0F, 1.0F));
  history.Record(2, Posed(2.0F, 1.0F, 1.0F, 0.0F, 1.0F));
  for (const double time : {0.0, 1.0, 1.25, 1.75, 2.0, 5.0}) {
    const std::optional<Pose> root = history.RootAt(time);
    const std::optional<Pose> pose = history.At(time, joints);
    ASSERT_TRUE(root.has_value());
    ASSERT_TRUE(pose.has_value());
    EXPECT_EQ(root->position, pose->position) << "at " << time;
    EXPECT_EQ(root->yaw, pose->yaw) << "at " << time;
    EXPECT_EQ(root->stance, pose->stance) << "at " << time;
  }
}

}  // namespace
