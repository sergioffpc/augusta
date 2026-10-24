#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/body_part.h"
#include "augusta/math.h"
#include "client_character_loader.h"

// A character's Hitboxes as a Development-build client loads them, for the
// Hitboxes Debug overlay (ADR-0074, ADR-0075): bound to their Joints and
// converted as the server does, then placed in the character's root space where
// its Rest pose holds them (ADR-0061).
namespace {

using augusta::assets::HitboxData;
using augusta::assets::JointData;
using augusta::assets::MeshData;
using augusta::assets::ResolveError;
using augusta::assets::Skeleton;
using augusta::body_part::BodyPart;
using augusta::client::CharacterErrorCode;
using augusta::client::DescribeCharacterError;
using augusta::client::LoadCharacterHitboxes;
using augusta::math::Mat4;
using augusta::math::Vec3;

MeshData Triangle(float height) {
  return {.points = {{0, height, 0}, {1, height, 0}, {0, height, 1}}, .indices = {0, 1, 2}};
}

// A Hitbox of part, bound to the root Joint.
HitboxData OnRoot(augusta::assets::BodyPart part, MeshData mesh) {
  return {.part = part, .joint = "Root", .mesh = std::move(mesh)};
}

// A Skeleton of its root Joint alone, at the character's root.
Skeleton RootAlone() { return {.joints = {JointData{.name = "Root"}}, .rest_transforms = {Mat4(1.0F)}}; }

auto SkeletonIs(Skeleton skeleton) {
  return [skeleton = std::move(skeleton)](std::string_view) { return std::expected<Skeleton, ResolveError>(skeleton); };
}

auto ResolveTo(std::vector<HitboxData> hitboxes, std::string* resolved = nullptr) {
  return [hitboxes = std::move(hitboxes), resolved](std::string_view character) {
    if (resolved != nullptr) {
      *resolved = character;
    }
    return std::expected<std::vector<HitboxData>, ResolveError>(hitboxes);
  };
}

TEST(LoadCharacterHitboxesTest, ConvertsEachOfTheCharactersHitboxesAsTheServerDoes) {
  std::string resolved;
  const auto hitboxes = LoadCharacterHitboxes("medic", SkeletonIs(RootAlone()),
                                              ResolveTo({OnRoot(augusta::assets::BodyPart::kHead, Triangle(1.7F)),
                                                         OnRoot(augusta::assets::BodyPart::kLimb, Triangle(0.4F))},
                                                        &resolved));

  ASSERT_TRUE(hitboxes.has_value());
  EXPECT_EQ(resolved, "medic");
  ASSERT_EQ(hitboxes->size(), 2U);
  EXPECT_EQ((*hitboxes)[0].part, BodyPart::kHead);
  ASSERT_EQ((*hitboxes)[0].triangles.size(), 1U);
  EXPECT_EQ((*hitboxes)[0].triangles[0].b, Vec3(1.0F, 1.7F, 0.0F));
  EXPECT_EQ((*hitboxes)[1].part, BodyPart::kLimb);
}

// A hand Joint held 2 m out to the right at 1.4 m: its Hitbox, in the hand's
// space, is drawn there, bound to the root as the overlay places it.
TEST(LoadCharacterHitboxesTest, PlacesEachHitboxInTheRootSpaceWhereTheRestPoseHoldsItsJoint) {
  const Vec3 hand(2.0F, 1.4F, 0.0F);
  const Skeleton reaching{
      .joints = {JointData{.name = "Root"}, JointData{.name = "Root/Hand", .parent = 0, .translation = hand}},
      .rest_transforms = {Mat4(1.0F),
                          augusta::math::ToMat4(hand, augusta::math::Quat(1.0F, 0.0F, 0.0F, 0.0F), Vec3(1.0F))}};

  const auto hitboxes = LoadCharacterHitboxes(
      "medic", SkeletonIs(reaching),
      ResolveTo({{.part = augusta::assets::BodyPart::kLimb, .joint = "Root/Hand", .mesh = Triangle(0.0F)}}));

  ASSERT_TRUE(hitboxes.has_value());
  ASSERT_EQ(hitboxes->size(), 1U);
  EXPECT_EQ((*hitboxes)[0].joint, 0U);
  ASSERT_EQ((*hitboxes)[0].triangles.size(), 1U);
  EXPECT_EQ((*hitboxes)[0].triangles[0].b, Vec3(3.0F, 1.4F, 0.0F));
}

TEST(LoadCharacterHitboxesTest, ACharacterWithoutHitboxesHasNone) {
  const auto hitboxes = LoadCharacterHitboxes("medic", SkeletonIs(RootAlone()), ResolveTo({}));

  ASSERT_TRUE(hitboxes.has_value());
  EXPECT_TRUE(hitboxes->empty());
}

TEST(LoadCharacterHitboxesTest, HitboxesThatDoNotResolveAreACharacterErrorNamingTheCharacter) {
  const auto hitboxes = LoadCharacterHitboxes("sniper", SkeletonIs(RootAlone()), [](std::string_view) {
    return std::expected<std::vector<HitboxData>, ResolveError>(std::unexpected(ResolveError::kCorruptBlob));
  });

  ASSERT_FALSE(hitboxes.has_value());
  EXPECT_EQ(hitboxes.error().code, CharacterErrorCode::kHitboxesUnresolved);
  EXPECT_EQ(hitboxes.error().character, "sniper");
  EXPECT_EQ(hitboxes.error().resolve_error, ResolveError::kCorruptBlob);
  EXPECT_NE(DescribeCharacterError(hitboxes.error()).find("sniper"), std::string::npos);
}

TEST(LoadCharacterHitboxesTest, ASkeletonThatDoesNotResolveIsACharacterErrorNamingTheCharacter) {
  const auto hitboxes = LoadCharacterHitboxes(
      "sniper",
      [](std::string_view) { return std::expected<Skeleton, ResolveError>(std::unexpected(ResolveError::kNotFound)); },
      ResolveTo({OnRoot(augusta::assets::BodyPart::kTorso, Triangle(1.0F))}));

  ASSERT_FALSE(hitboxes.has_value());
  EXPECT_EQ(hitboxes.error().code, CharacterErrorCode::kSkeletonUnresolved);
  EXPECT_EQ(hitboxes.error().character, "sniper");
  EXPECT_EQ(hitboxes.error().resolve_error, ResolveError::kNotFound);
  const std::string described = DescribeCharacterError(hitboxes.error());
  EXPECT_NE(described.find("sniper"), std::string::npos);
  EXPECT_NE(described.find("skeleton"), std::string::npos);
}

TEST(LoadCharacterHitboxesTest, AHitboxOnAJointTheSkeletonLacksIsACharacterErrorNamingTheJoint) {
  const auto hitboxes = LoadCharacterHitboxes(
      "sniper", SkeletonIs(RootAlone()),
      ResolveTo({{.part = augusta::assets::BodyPart::kHead, .joint = "Root/Neck", .mesh = Triangle(1.7F)}}));

  ASSERT_FALSE(hitboxes.has_value());
  EXPECT_EQ(hitboxes.error().code, CharacterErrorCode::kJointUnresolved);
  EXPECT_EQ(hitboxes.error().character, "sniper");
  EXPECT_EQ(hitboxes.error().subject, "Root/Neck");
  const std::string described = DescribeCharacterError(hitboxes.error());
  EXPECT_NE(described.find("sniper"), std::string::npos);
  EXPECT_NE(described.find("Root/Neck"), std::string::npos);
}

TEST(LoadCharacterHitboxesTest, AHitboxThatIsNotATriangleListIsACharacterErrorNamingItsBodyPart) {
  // The control: the same Hitbox with whole triangles loads.
  ASSERT_TRUE(LoadCharacterHitboxes("sniper", SkeletonIs(RootAlone()),
                                    ResolveTo({OnRoot(augusta::assets::BodyPart::kTorso, Triangle(1.0F))}))
                  .has_value());
  MeshData broken = Triangle(1.0F);
  broken.indices = {0, 1};

  const auto hitboxes = LoadCharacterHitboxes("sniper", SkeletonIs(RootAlone()),
                                              ResolveTo({OnRoot(augusta::assets::BodyPart::kTorso, broken)}));

  ASSERT_FALSE(hitboxes.has_value());
  EXPECT_EQ(hitboxes.error().code, CharacterErrorCode::kHitboxRejected);
  EXPECT_EQ(hitboxes.error().character, "sniper");
  EXPECT_EQ(hitboxes.error().subject, "torso");
  const std::string described = DescribeCharacterError(hitboxes.error());
  EXPECT_NE(described.find("sniper"), std::string::npos);
  EXPECT_NE(described.find("torso"), std::string::npos);
}

}  // namespace
