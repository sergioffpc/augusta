#include "augusta/map.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "encoder.h"

// Builds a real signed pack in a temp file and loads it back, so what is tested
// is the map module reading a Pack the way both executables do.
namespace {

using augusta::assets::AssetEntry;
using augusta::assets::AssetType;
using augusta::assets::MeshData;
using augusta::assets::Pack;
using augusta::assets::ResolveError;
using augusta::assets::SceneData;
using augusta::assets::SceneNode;
using augusta::map::LoadCollision;
using augusta::map::MapErrorCode;
using augusta::math::Vec3;

// A unit square on the ground plane, as a collision mesh blob.
std::vector<std::byte> SquareBlob() {
  MeshData mesh;
  mesh.points = {Vec3(0.0F, 0.0F, 0.0F), Vec3(0.0F, 0.0F, 1.0F), Vec3(1.0F, 0.0F, 1.0F), Vec3(1.0F, 0.0F, 0.0F)};
  mesh.indices = {0, 1, 2, 0, 2, 3};
  return augusta::assets::EncodeMeshBlob(mesh).value();
}

// A collision mesh with no triangles at all.
std::vector<std::byte> EmptyBlob() { return augusta::assets::EncodeMeshBlob(MeshData{}).value(); }

SceneNode Node(const std::string& name, std::uint32_t parent_index) {
  SceneNode node;
  node.name = name;
  node.parent_index = parent_index;
  return node;
}

std::vector<std::byte> SceneBlob(const std::vector<SceneNode>& nodes) {
  SceneData scene;
  scene.nodes = nodes;
  return augusta::assets::EncodeSceneBlob(scene).value();
}

class MapTest : public ::testing::Test {
 protected:
  void TearDown() override {
    for (const auto& path : cleanup_) {
      std::filesystem::remove(path);
    }
  }

  // Writes entries into a signed pack and loads it.
  Pack MakePack(const std::string& name, const std::vector<AssetEntry>& entries) {
    const auto path = std::filesystem::temp_directory_path() / ("augusta_map_test_" + name + ".pack");
    cleanup_.push_back(path);
    const auto keys = augusta::assets::GenerateEd25519KeyPair();
    EXPECT_TRUE(augusta::assets::WritePack(path, entries, keys.private_key).has_value());
    auto pack = Pack::Load(path, keys.public_key);
    EXPECT_TRUE(pack.has_value());
    return std::move(*pack);
  }

 private:
  std::vector<std::filesystem::path> cleanup_;
};

TEST_F(MapTest, PlacesEachColliderInWorldSpaceThroughItsParents) {
  SceneNode ground = Node("Ground", augusta::assets::kSceneNodeNoParent);
  ground.collider_path = "Ground";
  SceneNode crate = Node("Ground/Crate", 0);
  crate.translation = Vec3(10.0F, 0.0F, 0.0F);
  crate.collider_path = "Ground";
  const Pack pack = MakePack("world_space", {AssetEntry{AssetType::kCollision, "Ground", SquareBlob()},
                                             AssetEntry{AssetType::kScene, "Scene", SceneBlob({ground, crate})}});

  const auto collision = LoadCollision(pack);

  ASSERT_TRUE(collision.has_value());
  ASSERT_EQ(collision->size(), 2U);
  EXPECT_NEAR((*collision)[0].points[3].x, 1.0F, 1e-5F);
  EXPECT_NEAR((*collision)[1].points[3].x, 11.0F, 1e-5F);
  EXPECT_EQ((*collision)[1].indices.size(), 6U);
}

TEST_F(MapTest, NodesWithoutAColliderAddNothing) {
  SceneNode ground = Node("Ground", augusta::assets::kSceneNodeNoParent);
  ground.collider_path = "Ground";
  SceneNode decoration = Node("Decoration", augusta::assets::kSceneNodeNoParent);
  decoration.mesh_path = "Decoration";
  const Pack pack =
      MakePack("no_collider_nodes", {AssetEntry{AssetType::kCollision, "Ground", SquareBlob()},
                                     AssetEntry{AssetType::kScene, "Scene", SceneBlob({ground, decoration})}});

  const auto collision = LoadCollision(pack);

  ASSERT_TRUE(collision.has_value());
  EXPECT_EQ(collision->size(), 1U);
}

TEST_F(MapTest, APackWithNoCollisionAtAllIsAnError) {
  SceneNode decoration = Node("Decoration", augusta::assets::kSceneNodeNoParent);
  decoration.mesh_path = "Decoration";
  const Pack pack = MakePack("no_collision", {AssetEntry{AssetType::kScene, "Scene", SceneBlob({decoration})}});

  const auto collision = LoadCollision(pack);

  ASSERT_FALSE(collision.has_value());
  EXPECT_EQ(collision.error().code, MapErrorCode::kNoCollision);
}

TEST_F(MapTest, AColliderThatIsNotInThePackNamesIt) {
  SceneNode ground = Node("Ground", augusta::assets::kSceneNodeNoParent);
  ground.collider_path = "Missing";
  const Pack pack = MakePack("missing_collider", {AssetEntry{AssetType::kScene, "Scene", SceneBlob({ground})}});

  const auto collision = LoadCollision(pack);

  ASSERT_FALSE(collision.has_value());
  EXPECT_EQ(collision.error().code, MapErrorCode::kColliderUnresolved);
  EXPECT_EQ(collision.error().node, "Ground");
  EXPECT_EQ(collision.error().subject, "Missing");
  EXPECT_EQ(collision.error().resolve_error, ResolveError::kNotFound);
}

TEST_F(MapTest, AnEmptyColliderIsAnErrorNamingIt) {
  SceneNode ground = Node("Ground", augusta::assets::kSceneNodeNoParent);
  ground.collider_path = "Empty";
  const Pack pack = MakePack("empty_collider", {AssetEntry{AssetType::kCollision, "Empty", EmptyBlob()},
                                                AssetEntry{AssetType::kScene, "Scene", SceneBlob({ground})}});

  const auto collision = LoadCollision(pack);

  ASSERT_FALSE(collision.has_value());
  EXPECT_EQ(collision.error().code, MapErrorCode::kInvalidCollider);
  EXPECT_EQ(collision.error().node, "Ground");
  EXPECT_EQ(collision.error().subject, "Empty");
  EXPECT_EQ(collision.error().collision_mesh_error, augusta::physics::CollisionMeshError::kEmpty);
}

TEST_F(MapTest, APackWithoutASceneIsAnError) {
  const Pack pack = MakePack("no_scene", {AssetEntry{AssetType::kCollision, "Ground", SquareBlob()}});

  const auto collision = LoadCollision(pack);

  ASSERT_FALSE(collision.has_value());
  EXPECT_EQ(collision.error().code, MapErrorCode::kSceneUnresolved);
  EXPECT_EQ(collision.error().subject, std::string(augusta::assets::kScenePath));
}

TEST(DescribeMapErrorTest, ASceneOfTheWrongTypeIsNotDescribedAsCollisionGeometry) {
  const std::string message = augusta::map::DescribeMapError(
      {.code = MapErrorCode::kSceneUnresolved, .subject = "Scene", .resolve_error = ResolveError::kTypeMismatch});

  EXPECT_EQ(message, "scene Scene is not a scene");
}

TEST(DescribeMapErrorTest, NamesWhatWasMissing) {
  const std::string message = augusta::map::DescribeMapError({.code = MapErrorCode::kColliderUnresolved,
                                                              .node = "Ground",
                                                              .subject = "Missing",
                                                              .resolve_error = ResolveError::kNotFound});

  EXPECT_NE(message.find("Missing"), std::string::npos) << message;
  EXPECT_NE(message.find("Ground"), std::string::npos) << message;
}

}  // namespace
