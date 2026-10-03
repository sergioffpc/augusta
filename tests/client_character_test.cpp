#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "character_loader.h"

// Unit tests for resolving what the client needs of a character. Links no
// Falcor or window: the loader only uses renderer.h's plain mesh types.
namespace {

using augusta::assets::EyeData;
using augusta::assets::MeshData;
using augusta::assets::ResolveError;
using augusta::client::CharacterErrorCode;
using augusta::client::DescribeCharacterError;
using augusta::client::LoadCharacterEye;
using augusta::client::LoadCharacterMesh;
using augusta::math::Vec3;

constexpr float kTolerance = 1e-5F;
// A character's eye, in its own root space.
const Vec3 kEye{0.0F, 1.6F, 0.2F};

MeshData Triangle() { return {.points = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}, .indices = {0, 1, 2}}; }

auto ResolveNothing() {
  return [](std::string_view) -> std::expected<MeshData, ResolveError> {
    return std::unexpected(ResolveError::kNotFound);
  };
}

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

// A scenario's characters.
const std::vector<std::string> kCharacters = {"characters/sniper", "characters/medic"};

TEST(LoadCharacterMeshTest, ResolvesTheVisualMeshOfTheCharacterAPathNames) {
  std::string resolved;
  const auto mesh = LoadCharacterMesh(kCharacters, "characters/medic", [&](std::string_view path) {
    resolved = path;
    return std::expected<MeshData, ResolveError>(Triangle());
  });

  ASSERT_TRUE(mesh.has_value());
  EXPECT_EQ(resolved, "characters/medic/Character/Visual");
  // In the character's own root space: the renderer places it per player.
  ASSERT_EQ(mesh->positions.size(), 3U);
  ExpectNear(mesh->positions[1], {1.0F, 0.0F, 0.0F});
  EXPECT_EQ(mesh->indices, (std::vector<std::uint32_t>{0, 1, 2}));
}

TEST(LoadCharacterMeshTest, AMissingVisualMeshIsACharacterErrorNamingTheCharacter) {
  const auto mesh = LoadCharacterMesh(kCharacters, "characters/sniper", ResolveNothing());

  ASSERT_FALSE(mesh.has_value());
  EXPECT_EQ(mesh.error().code, CharacterErrorCode::kMeshUnresolved);
  EXPECT_EQ(mesh.error().character, "characters/sniper");
  EXPECT_EQ(mesh.error().subject, "characters/sniper/Character/Visual");
  EXPECT_EQ(mesh.error().resolve_error, ResolveError::kNotFound);
  EXPECT_NE(DescribeCharacterError(mesh.error()).find("characters/sniper"), std::string::npos);
}

TEST(LoadCharacterMeshTest, ACharacterOutsideThePacksCharacterListIsACharacterError) {
  for (const std::string_view character : {"", "characters/nobody", "characters/medic/"}) {
    const auto mesh = LoadCharacterMesh(kCharacters, character, ResolveNothing());

    ASSERT_FALSE(mesh.has_value()) << character;
    EXPECT_EQ(mesh.error().code, CharacterErrorCode::kUnknownCharacter);
    EXPECT_EQ(mesh.error().character, character);
    EXPECT_FALSE(DescribeCharacterError(mesh.error()).empty());
  }
}

TEST(LoadCharacterEyeTest, ResolvesTheEyeOfTheCharacterAPathNames) {
  std::string resolved;
  const auto eye = LoadCharacterEye("characters/medic", [&](std::string_view path) {
    resolved = path;
    return std::expected<EyeData, ResolveError>(EyeData{.position = kEye});
  });

  ASSERT_TRUE(eye.has_value());
  EXPECT_EQ(resolved, "characters/medic/Character/Eye");
  ExpectNear(*eye, kEye);
}

TEST(LoadCharacterEyeTest, AMissingEyeIsACharacterErrorNamingTheCharacter) {
  const auto eye = LoadCharacterEye("characters/sniper", [](std::string_view) {
    return std::expected<EyeData, ResolveError>(std::unexpected(ResolveError::kNotFound));
  });

  ASSERT_FALSE(eye.has_value());
  EXPECT_EQ(eye.error().code, CharacterErrorCode::kEyeUnresolved);
  EXPECT_EQ(eye.error().character, "characters/sniper");
  EXPECT_EQ(eye.error().subject, "characters/sniper/Character/Eye");
  EXPECT_EQ(eye.error().resolve_error, ResolveError::kNotFound);
  EXPECT_EQ(DescribeCharacterError(eye.error()),
            "eye characters/sniper/Character/Eye of character characters/sniper not found");
}

}  // namespace
