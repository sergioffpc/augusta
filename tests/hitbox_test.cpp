#include "augusta/hitbox.h"

#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/math.h"
#include "augusta/physics.h"

// Placing a Character's Hitboxes at a Hitbox pose (ADR-0044): where the server
// tests a round against them, and where a client's Debug overlay draws them;
// and converting a pack's Hitboxes into them, as both sides load them.
namespace {

using augusta::assets::HitboxData;
using augusta::ballistics::Triangle;
using augusta::body_part::BodyPart;
using augusta::hitbox::CharacterHitbox;
using augusta::hitbox::FromPack;
using augusta::hitbox::Place;
using augusta::hitbox::PlacedHitbox;
using augusta::hitbox::Pose;
using augusta::math::Vec3;
using augusta::physics::CollisionMeshError;
using augusta::physics::Stance;

constexpr float kTolerance = 1e-5F;

// A head a little in front of the root and a torso below it, as authored:
// standing, feet at the origin, facing -Z.
std::vector<CharacterHitbox> Character() {
  return {
      CharacterHitbox{
          .part = BodyPart::kHead,
          .triangles = {Triangle{.a = {0.0F, 1.8F, -0.5F}, .b = {0.1F, 1.8F, -0.5F}, .c = {0.0F, 1.9F, -0.5F}}}},
      CharacterHitbox{
          .part = BodyPart::kTorso,
          .triangles = {Triangle{.a = {0.0F, 1.0F, 0.0F}, .b = {0.2F, 1.0F, 0.0F}, .c = {0.0F, 1.2F, 0.0F}},
                        Triangle{.a = {0.2F, 1.0F, 0.0F}, .b = {0.2F, 1.2F, 0.0F}, .c = {0.0F, 1.2F, 0.0F}}}},
  };
}

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

TEST(HitboxTest, AStandingPoseAtTheOriginFacingAheadLeavesTheHitboxesAsAuthored) {
  const std::vector<PlacedHitbox> placed =
      Place(Character(), Pose{.position = Vec3(0.0F), .yaw = 0.0F, .stance = Stance::kStanding});

  ASSERT_EQ(placed.size(), 2U);
  ASSERT_EQ(placed[0].triangles.size(), 1U);
  ExpectNear(placed[0].triangles[0].a, Vec3(0.0F, 1.8F, -0.5F));
  ExpectNear(placed[0].triangles[0].b, Vec3(0.1F, 1.8F, -0.5F));
  ExpectNear(placed[0].triangles[0].c, Vec3(0.0F, 1.9F, -0.5F));
  EXPECT_EQ(placed[1].triangles.size(), 2U);
}

TEST(HitboxTest, EachHitboxKeepsItsBodyPart) {
  const std::vector<PlacedHitbox> placed =
      Place(Character(), Pose{.position = Vec3(3.0F, 0.0F, 4.0F), .yaw = 1.0F, .stance = Stance::kProne});

  ASSERT_EQ(placed.size(), 2U);
  EXPECT_EQ(placed[0].part, BodyPart::kHead);
  EXPECT_EQ(placed[1].part, BodyPart::kTorso);
}

TEST(HitboxTest, APoseMovesTheHitboxesToItsPosition) {
  const std::vector<PlacedHitbox> placed =
      Place(Character(), Pose{.position = Vec3(10.0F, 2.0F, 5.0F), .yaw = 0.0F, .stance = Stance::kStanding});

  ExpectNear(placed[0].triangles[0].a, Vec3(10.0F, 3.8F, 4.5F));
}

TEST(HitboxTest, APoseTurnsTheHitboxesByItsYawAboutTheVertical) {
  // A quarter turn left: what faced -Z faces -X.
  const std::vector<PlacedHitbox> placed = Place(
      Character(),
      Pose{.position = Vec3(10.0F, 0.0F, 5.0F), .yaw = std::numbers::pi_v<float> / 2.0F, .stance = Stance::kStanding});

  ExpectNear(placed[0].triangles[0].a, Vec3(9.5F, 1.8F, 5.0F));
  ExpectNear(placed[0].triangles[0].b, Vec3(9.5F, 1.8F, 4.9F));
}

// How low a body is in a stance is its stance's Clips' (ADR-0066): the root
// is not scaled by it (ADR-0040).
TEST(HitboxTest, TheStanceDoesNotScaleTheRoot) {
  for (const Stance stance : {Stance::kStanding, Stance::kCrouching, Stance::kProne}) {
    SCOPED_TRACE(static_cast<int>(stance));

    const std::vector<PlacedHitbox> placed =
        Place(Character(), Pose{.position = Vec3(1.0F, 0.0F, 0.0F), .yaw = 0.0F, .stance = stance});

    ExpectNear(placed[0].triangles[0].a, Vec3(1.0F, 1.8F, -0.5F));
    ExpectNear(placed[1].triangles[1].b, Vec3(1.2F, 1.2F, 0.0F));
  }
}

TEST(HitboxTest, ACharacterWithoutHitboxesPlacesNone) { EXPECT_TRUE(Place({}, Pose{}).empty()); }

HitboxData PackTriangle(augusta::assets::BodyPart part) {
  return {.part = part, .mesh = {.points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, .indices = {0, 1, 2}}};
}

TEST(HitboxTest, EveryBodyPartOfThePackIsTheSameBodyPart) {
  EXPECT_EQ(FromPack(augusta::assets::BodyPart::kHead), BodyPart::kHead);
  EXPECT_EQ(FromPack(augusta::assets::BodyPart::kTorso), BodyPart::kTorso);
  EXPECT_EQ(FromPack(augusta::assets::BodyPart::kLimb), BodyPart::kLimb);
}

TEST(HitboxTest, APackHitboxIsTheTrianglesItsMeshIndexes) {
  const auto hitbox = FromPack(PackTriangle(augusta::assets::BodyPart::kHead));

  ASSERT_TRUE(hitbox.has_value());
  EXPECT_EQ(hitbox->part, BodyPart::kHead);
  ASSERT_EQ(hitbox->triangles.size(), 1U);
  EXPECT_EQ(hitbox->triangles[0].a, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_EQ(hitbox->triangles[0].b, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(hitbox->triangles[0].c, Vec3(0.0F, 1.0F, 0.0F));
}

TEST(HitboxTest, APackHitboxThatIsNotAWholeTriangleListIsRefusedSayingWhy) {
  HitboxData hitbox = PackTriangle(augusta::assets::BodyPart::kTorso);
  ASSERT_TRUE(FromPack(hitbox).has_value());
  hitbox.mesh.indices = {0, 1};

  const auto refused = FromPack(hitbox);

  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), CollisionMeshError::kInvalidIndex);
}

}  // namespace
