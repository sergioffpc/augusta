#include "augusta/simulation_mapping.h"

#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/body_part.h"
#include "augusta/content.h"
#include "augusta/failure.h"
#include "augusta/match.h"
#include "augusta/math.h"

// Unit tests for the conversions Host makes at SimulationWorld's edge.
namespace {

using augusta::assets::BodyPart;
using augusta::assets::HitboxData;
using augusta::host::EntityId;
using augusta::host::FromSimulation;
using augusta::host::ToSimulation;
using augusta::math::Vec3;

HitboxData Triangle(BodyPart part) {
  return {.part = part, .mesh = {.points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, .indices = {0, 1, 2}}};
}

TEST(SimulationMappingTest, MatchAndSimulationWorldNameABodyByTheSameEntity) {
  EXPECT_EQ(FromSimulation(ToSimulation(EntityId{77})), EntityId{77});
  EXPECT_EQ(std::to_underlying(ToSimulation(EntityId{77})), 77U);
}

TEST(SimulationMappingTest, AHitboxIsTheTrianglesItsMeshIndexes) {
  const auto hitbox = ToSimulation(Triangle(BodyPart::kHead), "medic");

  EXPECT_EQ(hitbox.part, augusta::body_part::BodyPart::kHead);
  ASSERT_EQ(hitbox.triangles.size(), 1U);
  EXPECT_EQ(hitbox.triangles[0].a, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_EQ(hitbox.triangles[0].b, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(hitbox.triangles[0].c, Vec3(0.0F, 1.0F, 0.0F));
}

TEST(SimulationMappingTest, AHitboxThatIsNotAWholeTriangleListIsRejected) {
  HitboxData hitbox = Triangle(BodyPart::kTorso);
  hitbox.mesh.indices = {0, 1};

  try {
    static_cast<void>(ToSimulation(hitbox, "medic"));
    FAIL() << "a hitbox that is not a whole triangle list was taken";
  } catch (const augusta::failure::ClassifiedFailure& thrown) {
    EXPECT_EQ(thrown.GetFailure().code, augusta::failure::Code::kInvalidContent);
    ASSERT_FALSE(thrown.GetFailure().context.empty());
    EXPECT_EQ(thrown.GetFailure().context.front().value, "medic");
  }
}

// A Skeleton of one Joint, raised to height above the character's feet.
augusta::assets::Skeleton RootAt(float height) {
  return {.joints = {{.name = "Root", .translation = Vec3(0.0F, height, 0.0F)}},
          .rest_transforms = {augusta::math::ToMat4(Vec3(0.0F, height, 0.0F),
                                                    augusta::math::Quat(1.0F, 0.0F, 0.0F, 0.0F), Vec3(1.0F))}};
}

TEST(SimulationMappingTest, EachCharacterIsKeptByItsPath) {
  const std::vector<augusta::scenario::Character> characters = {
      {.path = "medic",
       .hitboxes = {{.joint = 0, .hitbox = Triangle(BodyPart::kHead)}},
       .skeleton = RootAt(0.0F),
       .eye = Vec3(0.0F, 1.6F, 0.0F)},
      {.path = "sniper", .hitboxes = {}, .eye = Vec3(0.0F, 1.5F, 0.0F)},
  };

  const auto converted = ToSimulation(characters);

  ASSERT_EQ(converted.size(), 2U);
  EXPECT_EQ(converted.at("medic").eye, Vec3(0.0F, 1.6F, 0.0F));
  EXPECT_EQ(converted.at("medic").hitboxes.size(), 1U);
  EXPECT_TRUE(converted.at("sniper").hitboxes.empty());
}

// Until SimulationWorld poses a Skeleton (ADR-0061), it judges a character's
// hitboxes where their Joints hold them in the Rest pose.
TEST(SimulationMappingTest, ACharactersHitboxesArePlacedByTheirJointsAtRest) {
  const std::vector<augusta::scenario::Character> characters = {
      {.path = "medic",
       .hitboxes = {{.joint = 0, .hitbox = Triangle(BodyPart::kHead)}},
       .skeleton = RootAt(1.5F),
       .eye = Vec3(0.0F, 1.6F, 0.0F)},
  };

  const auto converted = ToSimulation(characters);

  ASSERT_EQ(converted.at("medic").hitboxes.size(), 1U);
  ASSERT_EQ(converted.at("medic").hitboxes[0].triangles.size(), 1U);
  EXPECT_EQ(converted.at("medic").hitboxes[0].triangles[0].a, Vec3(0.0F, 1.5F, 0.0F));
  EXPECT_EQ(converted.at("medic").hitboxes[0].triangles[0].c, Vec3(0.0F, 2.5F, 0.0F));
}

}  // namespace
