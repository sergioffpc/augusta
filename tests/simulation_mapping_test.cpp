#include "augusta/simulation_mapping.h"

#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/body_part.h"
#include "augusta/command.h"
#include "augusta/content.h"
#include "augusta/failure.h"
#include "augusta/match.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/simulation.h"
#include "augusta/weapon.h"

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

// SimulationWorld poses a character's hitboxes by its Skeleton (ADR-0061): it
// is handed both, each hitbox in its Joint's space and bound to it.
TEST(SimulationMappingTest, ACharacterKeepsItsSkeletonAndEachHitboxInItsJointsSpace) {
  const std::vector<augusta::scenario::Character> characters = {
      {.path = "medic",
       .hitboxes = {{.joint = 0, .hitbox = Triangle(BodyPart::kHead)}},
       .skeleton = RootAt(1.5F),
       .eye = Vec3(0.0F, 1.6F, 0.0F)},
  };

  const auto converted = ToSimulation(characters);

  const augusta::simulation::Character& medic = converted.at("medic");
  ASSERT_EQ(medic.skeleton.joints.size(), 1U);
  EXPECT_EQ(medic.skeleton.joints[0].name, "Root");
  ASSERT_EQ(medic.hitboxes.size(), 1U);
  EXPECT_EQ(medic.hitboxes[0].joint, 0U);
  ASSERT_EQ(medic.hitboxes[0].triangles.size(), 1U);
  EXPECT_EQ(medic.hitboxes[0].triangles[0].a, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_EQ(medic.hitboxes[0].triangles[0].c, Vec3(0.0F, 1.0F, 0.0F));
}

// Every server places its players' Muzzles from the scenario's rifle (ADR-0064):
// in ADS its Sight is on the Eye and its Muzzle where the rifle has it ahead of
// its Sight.
TEST(SimulationMappingTest, ASimulationHoldsTheScenariosRifle) {
  augusta::scenario::Scenario scenario;
  scenario.rifle = augusta::weapon::Markers{
      .grip = augusta::math::Mat4(1.0F),
      .muzzle =
          augusta::math::ToMat4(Vec3(0.0F, 0.06F, -0.62F), augusta::math::Quat(1.0F, 0.0F, 0.0F, 0.0F), Vec3(1.0F)),
      .sight =
          augusta::math::ToMat4(Vec3(0.0F, 0.12F, -0.05F), augusta::math::Quat(1.0F, 0.0F, 0.0F, 0.0F), Vec3(1.0F)),
  };
  auto world = augusta::host::BuildSimulation(augusta::parameters::Parameters{}, 60, scenario, {});
  const augusta::simulation::EntityId alice{1};
  world.AddPlayer(alice, Vec3(0.0F, 0.0F, 0.0F),
                  augusta::simulation::Character{.eye = Vec3(0.0F, 1.6F, 0.0F), .hitboxes = {}});

  augusta::command::Command aiming;
  aiming.ads = true;
  static_cast<void>(world.Tick({{.entity = alice, .command = aiming}}, 1.0F / 60.0F));

  const auto held = world.HeldRifleOf(alice);
  ASSERT_TRUE(held.has_value());
  const Vec3 muzzle_from_sight = Vec3(held->muzzle[3]) - Vec3(held->sight[3]);
  EXPECT_NEAR(augusta::math::Length(muzzle_from_sight - Vec3(0.0F, -0.06F, -0.57F)), 0.0F, 1e-4F);
}

}  // namespace
