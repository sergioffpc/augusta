#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "scene_loader.h"

// Unit tests for the cooked-scene -> renderer::Scene conversion. Links no
// Falcor or window: the loader only uses renderer.h's plain scene types.
namespace {

using augusta::assets::EyeData;
using augusta::assets::MeshData;
using augusta::assets::ResolveError;
using augusta::assets::SceneData;
using augusta::assets::SceneNode;
using augusta::client::BuildRenderScene;
using augusta::client::CharactersToLoad;
using augusta::client::DescribeSceneError;
using augusta::client::LoadCharacterEye;
using augusta::client::LoadCharacterMesh;
using augusta::client::SceneErrorCode;
using augusta::math::Quat;
using augusta::math::Vec3;

constexpr float kTolerance = 1e-5F;
constexpr float kQuarterTurn = 1.5707963F;
// The local player's eye, in its character's root space (LoadCharacterEye).
const Vec3 kEye{0.0F, 1.6F, 0.2F};

MeshData Triangle() { return {.points = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}, .indices = {0, 1, 2}}; }

auto ResolveTriangle() {
  return [](std::string_view path) -> std::expected<MeshData, ResolveError> {
    if (path == "Root/Tri") {
      return Triangle();
    }
    return std::unexpected(ResolveError::kNotFound);
  };
}

SceneNode Node(std::string name, std::uint32_t parent_index) {
  SceneNode node;
  node.name = std::move(name);
  node.parent_index = parent_index;
  return node;
}

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

TEST(BuildRenderSceneTest, PlacesMeshGeometryInWorldSpaceThroughTheParentChain) {
  SceneData scene;
  scene.nodes.push_back(Node("Root", augusta::assets::kSceneNodeNoParent));
  scene.nodes.back().translation = {10.0F, 0.0F, 0.0F};
  scene.nodes.push_back(Node("Root/Tri", 0));
  scene.nodes.back().translation = {0.0F, 2.0F, 0.0F};
  scene.nodes.back().mesh_path = "Root/Tri";

  const auto result = BuildRenderScene(scene, ResolveTriangle(), kEye);

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->meshes.size(), 1U);
  ASSERT_EQ(result->meshes[0].positions.size(), 3U);
  ExpectNear(result->meshes[0].positions[0], {10.0F, 2.0F, 0.0F});
  ExpectNear(result->meshes[0].positions[1], {11.0F, 2.0F, 0.0F});
  ExpectNear(result->meshes[0].positions[2], {10.0F, 2.0F, 1.0F});
  EXPECT_EQ(result->meshes[0].indices, (std::vector<std::uint32_t>{0, 1, 2}));
}

TEST(BuildRenderSceneTest, ReadsAMeshsBaseColorFromItsNodeProperty) {
  SceneData scene;
  scene.nodes.push_back(Node("Root/Tri", augusta::assets::kSceneNodeNoParent));
  scene.nodes.back().mesh_path = "Root/Tri";
  scene.nodes.back().properties.emplace_back("base_color", "0.25 0.5 1");

  const auto result = BuildRenderScene(scene, ResolveTriangle(), kEye);

  ASSERT_TRUE(result.has_value());
  ExpectNear(result->meshes[0].color, {0.25F, 0.5F, 1.0F});
}

TEST(BuildRenderSceneTest, KeepsTheDefaultColorWhenANodeHasNoBaseColor) {
  SceneData scene;
  scene.nodes.push_back(Node("Root/Tri", augusta::assets::kSceneNodeNoParent));
  scene.nodes.back().mesh_path = "Root/Tri";

  const auto result = BuildRenderScene(scene, ResolveTriangle(), kEye);

  ASSERT_TRUE(result.has_value());
  ExpectNear(result->meshes[0].color, augusta::renderer::SceneMesh{}.color);
}

TEST(BuildRenderSceneTest, RejectsAMalformedBaseColorNamingTheNode) {
  for (const char* value : {"0.5 0.5", "red", "0.1 0.2 0.3 0.4", ""}) {
    SceneData scene;
    scene.nodes.push_back(Node("Root/Tri", augusta::assets::kSceneNodeNoParent));
    scene.nodes.back().mesh_path = "Root/Tri";
    scene.nodes.back().properties.emplace_back("base_color", value);

    const auto result = BuildRenderScene(scene, ResolveTriangle(), kEye);

    ASSERT_FALSE(result.has_value()) << value;
    EXPECT_EQ(result.error().code, SceneErrorCode::kMalformedBaseColor) << value;
    EXPECT_EQ(result.error().node, "Root/Tri") << value;
    EXPECT_EQ(result.error().subject, value);
  }
}

TEST(BuildRenderSceneTest, AppliesAParentsRotationToItsChildren) {
  SceneData scene;
  scene.nodes.push_back(Node("Root", augusta::assets::kSceneNodeNoParent));
  scene.nodes.back().rotation = glm::angleAxis(kQuarterTurn, Vec3(0.0F, 1.0F, 0.0F));
  scene.nodes.push_back(Node("Root/Tri", 0));
  scene.nodes.back().mesh_path = "Root/Tri";

  const auto result = BuildRenderScene(scene, ResolveTriangle(), kEye);

  ASSERT_TRUE(result.has_value());
  // A quarter turn about +Y takes +X to -Z and +Z to +X.
  ExpectNear(result->meshes[0].positions[1], {0.0F, 0.0F, -1.0F});
  ExpectNear(result->meshes[0].positions[2], {1.0F, 0.0F, 0.0F});
}

TEST(BuildRenderSceneTest, PutsTheCameraAtTheLocalPlayersEyeAboveTheFirstSpawnPoint) {
  SceneData scene;
  scene.nodes.push_back(Node("Root", augusta::assets::kSceneNodeNoParent));
  scene.nodes.push_back(Node("Root/SpawnA", 0));
  scene.nodes.back().is_spawn_point = true;
  scene.nodes.back().translation = {1.0F, 0.0F, 5.0F};
  scene.nodes.back().rotation = glm::angleAxis(kQuarterTurn, Vec3(0.0F, 1.0F, 0.0F));
  scene.nodes.push_back(Node("Root/SpawnB", 0));
  scene.nodes.back().is_spawn_point = true;
  scene.nodes.back().translation = {9.0F, 0.0F, 9.0F};

  const auto result = BuildRenderScene(scene, ResolveTriangle(), kEye);

  ASSERT_TRUE(result.has_value());
  ExpectNear(result->camera.position, Vec3(1.0F, 0.0F, 5.0F) + kEye);
  const Quat expected = glm::angleAxis(kQuarterTurn, Vec3(0.0F, 1.0F, 0.0F));
  EXPECT_NEAR(result->camera.rotation.w, expected.w, kTolerance);
  EXPECT_NEAR(result->camera.rotation.y, expected.y, kTolerance);
}

TEST(BuildRenderSceneTest, KeepsTheDefaultCameraWithoutASpawnPoint) {
  SceneData scene;
  scene.nodes.push_back(Node("Root", augusta::assets::kSceneNodeNoParent));

  const auto result = BuildRenderScene(scene, ResolveTriangle(), kEye);

  ASSERT_TRUE(result.has_value());
  const augusta::renderer::Camera default_camera;
  ExpectNear(result->camera.position, default_camera.position);
}

TEST(BuildRenderSceneTest, ReportsTheMeshItCouldNotResolve) {
  SceneData scene;
  scene.nodes.push_back(Node("Root", augusta::assets::kSceneNodeNoParent));
  scene.nodes.back().mesh_path = "Root/Missing";

  const auto result = BuildRenderScene(scene, ResolveTriangle(), kEye);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, SceneErrorCode::kMeshUnresolved);
  EXPECT_EQ(result.error().node, "Root");
  EXPECT_EQ(result.error().subject, "Root/Missing");
  EXPECT_EQ(result.error().resolve_error, ResolveError::kNotFound);
}

TEST(DescribeSceneErrorTest, NamesTheAssetThatFailedToResolve) {
  const auto scene = DescribeSceneError(
      {.code = SceneErrorCode::kSceneUnresolved, .subject = "Scene", .resolve_error = ResolveError::kTypeMismatch});
  const auto mesh = DescribeSceneError({.code = SceneErrorCode::kMeshUnresolved,
                                        .node = "Root",
                                        .subject = "Root/Missing",
                                        .resolve_error = ResolveError::kNotFound});

  EXPECT_EQ(scene, "scene Scene is not a scene");
  EXPECT_EQ(mesh, "mesh Root/Missing of node Root not found");
}

// A scenario's characters, in manifest order: index 1 is the sniper, 2 the medic.
const std::vector<std::string> kCharacters = {"characters/sniper", "characters/medic"};

TEST(CharactersToLoadTest, AreTheOtherPlayersCharactersNotLoadedYetEachOnce) {
  const std::vector<std::uint8_t> others = {3, 1, 3, 2};

  EXPECT_EQ(CharactersToLoad(others, {}), (std::vector<std::uint8_t>{3, 1, 2}));
  EXPECT_EQ(CharactersToLoad(others, {1}), (std::vector<std::uint8_t>{3, 2}));
  EXPECT_TRUE(CharactersToLoad(others, {1, 2, 3}).empty());
  EXPECT_TRUE(CharactersToLoad({}, {}).empty());
}

TEST(LoadCharacterMeshTest, ResolvesTheVisualMeshOfTheCharacterAnIndexNames) {
  std::string resolved;
  const auto mesh = LoadCharacterMesh(kCharacters, 2, [&](std::string_view path) {
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

TEST(LoadCharacterMeshTest, AMissingVisualMeshIsASceneErrorNamingTheCharacter) {
  const auto mesh = LoadCharacterMesh(kCharacters, 1, ResolveTriangle());

  ASSERT_FALSE(mesh.has_value());
  EXPECT_EQ(mesh.error().code, SceneErrorCode::kCharacterMeshUnresolved);
  EXPECT_EQ(mesh.error().node, "characters/sniper");
  EXPECT_EQ(mesh.error().subject, "characters/sniper/Character/Visual");
  EXPECT_EQ(mesh.error().resolve_error, ResolveError::kNotFound);
  EXPECT_NE(DescribeSceneError(mesh.error()).find("characters/sniper"), std::string::npos);
}

TEST(LoadCharacterMeshTest, AnIndexOutsideThePacksCharacterListIsASceneError) {
  for (const std::uint8_t index : {std::uint8_t{0}, std::uint8_t{3}, std::uint8_t{255}}) {
    const auto mesh = LoadCharacterMesh(kCharacters, index, ResolveTriangle());

    ASSERT_FALSE(mesh.has_value()) << static_cast<int>(index);
    EXPECT_EQ(mesh.error().code, SceneErrorCode::kUnknownCharacter);
    EXPECT_EQ(mesh.error().subject, std::to_string(index));
    EXPECT_FALSE(DescribeSceneError(mesh.error()).empty());
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

TEST(LoadCharacterEyeTest, AMissingEyeIsASceneErrorNamingTheCharacter) {
  const auto eye = LoadCharacterEye("characters/sniper", [](std::string_view) {
    return std::expected<EyeData, ResolveError>(std::unexpected(ResolveError::kNotFound));
  });

  ASSERT_FALSE(eye.has_value());
  EXPECT_EQ(eye.error().code, SceneErrorCode::kCharacterEyeUnresolved);
  EXPECT_EQ(eye.error().node, "characters/sniper");
  EXPECT_EQ(eye.error().subject, "characters/sniper/Character/Eye");
  EXPECT_EQ(eye.error().resolve_error, ResolveError::kNotFound);
  EXPECT_EQ(DescribeSceneError(eye.error()),
            "eye characters/sniper/Character/Eye of character characters/sniper not found");
}

}  // namespace
