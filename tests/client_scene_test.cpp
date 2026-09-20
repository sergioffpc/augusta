#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "scene_loader.h"

// Unit tests for the cooked-scene -> renderer::Scene conversion. Links no
// Falcor or window: the loader only uses renderer.h's plain scene types.
namespace {

using augusta::assets::MeshData;
using augusta::assets::ResolveError;
using augusta::assets::SceneData;
using augusta::assets::SceneNode;
using augusta::client::BuildRenderScene;
using augusta::client::DescribeSceneError;
using augusta::client::SceneErrorCode;
using augusta::math::Quat;
using augusta::math::Vec3;

constexpr float kTolerance = 1e-5F;
constexpr float kQuarterTurn = 1.5707963F;

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

  const auto result = BuildRenderScene(scene, ResolveTriangle());

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

  const auto result = BuildRenderScene(scene, ResolveTriangle());

  ASSERT_TRUE(result.has_value());
  ExpectNear(result->meshes[0].color, {0.25F, 0.5F, 1.0F});
}

TEST(BuildRenderSceneTest, KeepsTheDefaultColorWhenANodeHasNoBaseColor) {
  SceneData scene;
  scene.nodes.push_back(Node("Root/Tri", augusta::assets::kSceneNodeNoParent));
  scene.nodes.back().mesh_path = "Root/Tri";

  const auto result = BuildRenderScene(scene, ResolveTriangle());

  ASSERT_TRUE(result.has_value());
  ExpectNear(result->meshes[0].color, augusta::renderer::SceneMesh{}.color);
}

TEST(BuildRenderSceneTest, RejectsAMalformedBaseColorNamingTheNode) {
  for (const char* value : {"0.5 0.5", "red", "0.1 0.2 0.3 0.4", ""}) {
    SceneData scene;
    scene.nodes.push_back(Node("Root/Tri", augusta::assets::kSceneNodeNoParent));
    scene.nodes.back().mesh_path = "Root/Tri";
    scene.nodes.back().properties.emplace_back("base_color", value);

    const auto result = BuildRenderScene(scene, ResolveTriangle());

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

  const auto result = BuildRenderScene(scene, ResolveTriangle());

  ASSERT_TRUE(result.has_value());
  // A quarter turn about +Y takes +X to -Z and +Z to +X.
  ExpectNear(result->meshes[0].positions[1], {0.0F, 0.0F, -1.0F});
  ExpectNear(result->meshes[0].positions[2], {1.0F, 0.0F, 0.0F});
}

TEST(BuildRenderSceneTest, PutsTheCameraAtTheFirstSpawnPointAtEyeHeight) {
  SceneData scene;
  scene.nodes.push_back(Node("Root", augusta::assets::kSceneNodeNoParent));
  scene.nodes.push_back(Node("Root/SpawnA", 0));
  scene.nodes.back().is_spawn_point = true;
  scene.nodes.back().translation = {1.0F, 0.0F, 5.0F};
  scene.nodes.back().rotation = glm::angleAxis(kQuarterTurn, Vec3(0.0F, 1.0F, 0.0F));
  scene.nodes.push_back(Node("Root/SpawnB", 0));
  scene.nodes.back().is_spawn_point = true;
  scene.nodes.back().translation = {9.0F, 0.0F, 9.0F};

  const auto result = BuildRenderScene(scene, ResolveTriangle());

  ASSERT_TRUE(result.has_value());
  EXPECT_NEAR(result->camera.position.x, 1.0F, kTolerance);
  EXPECT_NEAR(result->camera.position.y, 1.7F, kTolerance);
  EXPECT_NEAR(result->camera.position.z, 5.0F, kTolerance);
  const Quat expected = glm::angleAxis(kQuarterTurn, Vec3(0.0F, 1.0F, 0.0F));
  EXPECT_NEAR(result->camera.rotation.w, expected.w, kTolerance);
  EXPECT_NEAR(result->camera.rotation.y, expected.y, kTolerance);
}

TEST(BuildRenderSceneTest, KeepsTheDefaultCameraWithoutASpawnPoint) {
  SceneData scene;
  scene.nodes.push_back(Node("Root", augusta::assets::kSceneNodeNoParent));

  const auto result = BuildRenderScene(scene, ResolveTriangle());

  ASSERT_TRUE(result.has_value());
  const augusta::renderer::Camera default_camera;
  ExpectNear(result->camera.position, default_camera.position);
}

TEST(BuildRenderSceneTest, ReportsTheMeshItCouldNotResolve) {
  SceneData scene;
  scene.nodes.push_back(Node("Root", augusta::assets::kSceneNodeNoParent));
  scene.nodes.back().mesh_path = "Root/Missing";

  const auto result = BuildRenderScene(scene, ResolveTriangle());

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

}  // namespace
