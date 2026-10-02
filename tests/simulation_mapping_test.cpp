#include "simulation_mapping.h"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/math.h"
#include "augusta/simulation.h"
#include "content.h"
#include "match.h"

// Unit tests for the conversions Host makes at SimulationWorld's edge.
namespace {

using augusta::assets::BodyPart;
using augusta::assets::HitboxData;
using augusta::math::Vec3;
using augusta::server::EntityId;
using augusta::server::FromSimulation;
using augusta::server::SessionId;
using augusta::server::ToBallistics;
using augusta::server::ToSimulation;

HitboxData Triangle(BodyPart part) {
  return {.part = part, .mesh = {.points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, .indices = {0, 1, 2}}};
}

TEST(SimulationMappingTest, MatchAndSimulationWorldNameABodyByTheSameEntity) {
  EXPECT_EQ(FromSimulation(ToSimulation(EntityId{77})), EntityId{77});
  EXPECT_EQ(std::to_underlying(ToSimulation(EntityId{77})), 77U);
}

TEST(SimulationMappingTest, MatchAndGamePolicyNameAPlayerByTheSameSession) {
  EXPECT_EQ(FromSimulation(ToSimulation(SessionId{5})), SessionId{5});
  EXPECT_EQ(std::to_underlying(ToSimulation(SessionId{5})), 5U);
}

TEST(SimulationMappingTest, EveryBodyPartIsTheSameBodyPartToBallistics) {
  EXPECT_EQ(ToBallistics(BodyPart::kHead), augusta::ballistics::BodyPart::kHead);
  EXPECT_EQ(ToBallistics(BodyPart::kTorso), augusta::ballistics::BodyPart::kTorso);
  EXPECT_EQ(ToBallistics(BodyPart::kLimb), augusta::ballistics::BodyPart::kLimb);
}

TEST(SimulationMappingTest, AHitboxIsTheTrianglesItsMeshIndexes) {
  const auto hitbox = ToSimulation(Triangle(BodyPart::kHead), "characters/medic");

  EXPECT_EQ(hitbox.part, augusta::ballistics::BodyPart::kHead);
  ASSERT_EQ(hitbox.triangles.size(), 1U);
  EXPECT_EQ(hitbox.triangles[0].a, Vec3(0.0F, 0.0F, 0.0F));
  EXPECT_EQ(hitbox.triangles[0].b, Vec3(1.0F, 0.0F, 0.0F));
  EXPECT_EQ(hitbox.triangles[0].c, Vec3(0.0F, 1.0F, 0.0F));
}

TEST(SimulationMappingTest, AHitboxThatIsNotAWholeTriangleListIsRejected) {
  HitboxData hitbox = Triangle(BodyPart::kTorso);
  hitbox.mesh.indices = {0, 1};

  EXPECT_THROW(static_cast<void>(ToSimulation(hitbox, "characters/medic")), std::runtime_error);
}

TEST(SimulationMappingTest, EachCharacterIsKeptByItsPath) {
  const std::vector<augusta::server::Character> characters = {
      {.path = "characters/medic", .hitboxes = {Triangle(BodyPart::kHead)}, .eye = Vec3(0.0F, 1.6F, 0.0F)},
      {.path = "characters/sniper", .hitboxes = {}, .eye = Vec3(0.0F, 1.5F, 0.0F)},
  };

  const auto converted = ToSimulation(characters);

  ASSERT_EQ(converted.size(), 2U);
  EXPECT_EQ(converted.at("characters/medic").eye, Vec3(0.0F, 1.6F, 0.0F));
  EXPECT_EQ(converted.at("characters/medic").hitboxes.size(), 1U);
  EXPECT_TRUE(converted.at("characters/sniper").hitboxes.empty());
}

}  // namespace
