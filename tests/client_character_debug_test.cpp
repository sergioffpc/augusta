#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/body_part.h"
#include "augusta/client_character_loader.h"
#include "augusta/math.h"

// A character's Hitboxes as a Development-build client loads them, for the
// Hitboxes Debug overlay (ADR-0074, ADR-0075): with the server's conversion.
namespace {

using augusta::assets::HitboxData;
using augusta::assets::MeshData;
using augusta::assets::ResolveError;
using augusta::body_part::BodyPart;
using augusta::client::CharacterErrorCode;
using augusta::client::DescribeCharacterError;
using augusta::client::LoadCharacterHitboxes;
using augusta::math::Vec3;

MeshData Triangle(float height) {
  return {.points = {{0, height, 0}, {1, height, 0}, {0, height, 1}}, .indices = {0, 1, 2}};
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
  const auto hitboxes =
      LoadCharacterHitboxes("medic", ResolveTo({{.part = augusta::assets::BodyPart::kHead, .mesh = Triangle(1.7F)},
                                                {.part = augusta::assets::BodyPart::kLimb, .mesh = Triangle(0.4F)}},
                                               &resolved));

  ASSERT_TRUE(hitboxes.has_value());
  EXPECT_EQ(resolved, "medic");
  ASSERT_EQ(hitboxes->size(), 2U);
  EXPECT_EQ((*hitboxes)[0].part, BodyPart::kHead);
  ASSERT_EQ((*hitboxes)[0].triangles.size(), 1U);
  EXPECT_EQ((*hitboxes)[0].triangles[0].b, Vec3(1.0F, 1.7F, 0.0F));
  EXPECT_EQ((*hitboxes)[1].part, BodyPart::kLimb);
}

TEST(LoadCharacterHitboxesTest, ACharacterWithoutHitboxesHasNone) {
  const auto hitboxes = LoadCharacterHitboxes("medic", ResolveTo({}));

  ASSERT_TRUE(hitboxes.has_value());
  EXPECT_TRUE(hitboxes->empty());
}

TEST(LoadCharacterHitboxesTest, HitboxesThatDoNotResolveAreACharacterErrorNamingTheCharacter) {
  const auto hitboxes = LoadCharacterHitboxes("sniper", [](std::string_view) {
    return std::expected<std::vector<HitboxData>, ResolveError>(std::unexpected(ResolveError::kCorruptBlob));
  });

  ASSERT_FALSE(hitboxes.has_value());
  EXPECT_EQ(hitboxes.error().code, CharacterErrorCode::kHitboxesUnresolved);
  EXPECT_EQ(hitboxes.error().character, "sniper");
  EXPECT_EQ(hitboxes.error().resolve_error, ResolveError::kCorruptBlob);
  EXPECT_NE(DescribeCharacterError(hitboxes.error()).find("sniper"), std::string::npos);
}

TEST(LoadCharacterHitboxesTest, AHitboxThatIsNotATriangleListIsACharacterErrorNamingItsBodyPart) {
  // The control: the same Hitbox with whole triangles loads.
  ASSERT_TRUE(
      LoadCharacterHitboxes("sniper", ResolveTo({{.part = augusta::assets::BodyPart::kTorso, .mesh = Triangle(1.0F)}}))
          .has_value());
  MeshData broken = Triangle(1.0F);
  broken.indices = {0, 1};

  const auto hitboxes =
      LoadCharacterHitboxes("sniper", ResolveTo({{.part = augusta::assets::BodyPart::kTorso, .mesh = broken}}));

  ASSERT_FALSE(hitboxes.has_value());
  EXPECT_EQ(hitboxes.error().code, CharacterErrorCode::kHitboxRejected);
  EXPECT_EQ(hitboxes.error().character, "sniper");
  EXPECT_EQ(hitboxes.error().subject, "torso");
  const std::string described = DescribeCharacterError(hitboxes.error());
  EXPECT_NE(described.find("sniper"), std::string::npos);
  EXPECT_NE(described.find("torso"), std::string::npos);
}

}  // namespace
