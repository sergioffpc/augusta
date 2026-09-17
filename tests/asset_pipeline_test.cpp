#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

#include "augusta/asset_cooking.h"
#include "augusta/assets.h"
#include "augusta/math.h"

// Round-trips the asset-cooking pipeline (ROADMAP.md M2, issue #46) end to
// end: cooks a fixture USD stage into a signed pack and loads it back,
// covering both the happy path (mesh + scene graph, ADR-0032) and the
// pipeline's fail-closed behavior on malformed/tampered input
// (ADR-0030/ADR-0031's hash-then-sign trailer).
namespace {

using augusta::asset_cooking::Cook;
using augusta::asset_cooking::CookError;
using augusta::assets::Ed25519KeyPair;
using augusta::assets::Pack;
using augusta::math::Vec3;

std::filesystem::path FixturePath(std::string_view name) {
  return std::filesystem::path(AUGUSTA_ASSET_PIPELINE_FIXTURE_DIR) / name;
}

std::filesystem::path TempPackPath(std::string_view name) { return std::filesystem::temp_directory_path() / name; }

std::vector<std::byte> ReadFileBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  const auto size = static_cast<std::size_t>(file.tellg());
  file.seekg(0);
  std::vector<std::byte> bytes(size);
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
  return bytes;
}

void WriteFileBytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

class AssetPipelineTest : public ::testing::Test {
 protected:
  void TearDown() override {
    for (const auto& path : cleanup_) {
      std::filesystem::remove(path);
    }
  }

  std::filesystem::path MakePackPath(std::string_view name) {
    const auto path = TempPackPath(name);
    cleanup_.push_back(path);
    return path;
  }

 private:
  std::vector<std::filesystem::path> cleanup_;
};

TEST_F(AssetPipelineTest, CooksFixtureMeshAndLoadsItBack) {
  const auto pack_path = MakePackPath("augusta_asset_pipeline_test_mesh.pack");
  const auto keys = augusta::assets::GenerateEd25519KeyPair();

  const auto report = Cook(FixturePath("mesh_fixture.usda"), pack_path, keys.private_key);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->mesh_count, 1U);
  EXPECT_EQ(report->node_count, 1U);

  auto pack = Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto mesh = pack->ResolveMesh("TestMesh");
  ASSERT_TRUE(mesh.has_value());

  const std::vector<Vec3> expected_points = {
      Vec3(0.0F, 0.0F, 0.0F),
      Vec3(1.0F, 0.0F, 0.0F),
      Vec3(1.0F, 1.0F, 0.0F),
      Vec3(0.0F, 1.0F, 0.0F),
  };
  const std::vector<std::uint32_t> expected_indices = {0, 1, 2, 0, 2, 3};

  ASSERT_EQ(mesh->points.size(), expected_points.size());
  for (std::size_t i = 0; i < expected_points.size(); ++i) {
    EXPECT_FLOAT_EQ(mesh->points[i].x, expected_points[i].x) << "point " << i;
    EXPECT_FLOAT_EQ(mesh->points[i].y, expected_points[i].y) << "point " << i;
    EXPECT_FLOAT_EQ(mesh->points[i].z, expected_points[i].z) << "point " << i;
  }
  EXPECT_EQ(mesh->indices, expected_indices);
}

TEST_F(AssetPipelineTest, CooksFixtureSceneGraphAndLoadsItBack) {
  const auto pack_path = MakePackPath("augusta_asset_pipeline_test_scene.pack");
  const auto keys = augusta::assets::GenerateEd25519KeyPair();

  const auto report = Cook(FixturePath("scene_fixture.usda"), pack_path, keys.private_key);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->mesh_count, 1U);
  EXPECT_EQ(report->node_count, 3U);  // Root, Root/Child, Root/Spawn

  auto pack = Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto scene = pack->ResolveScene("Scene");
  ASSERT_TRUE(scene.has_value());
  ASSERT_EQ(scene->nodes.size(), 3U);

  const auto& root = scene->nodes[0];
  EXPECT_EQ(root.name, "Root");
  EXPECT_EQ(root.parent_index, augusta::assets::kSceneNodeNoParent);
  EXPECT_FLOAT_EQ(root.translation.x, 1.0F);
  EXPECT_FLOAT_EQ(root.translation.y, 2.0F);
  EXPECT_FLOAT_EQ(root.translation.z, 3.0F);
  EXPECT_FALSE(root.mesh_path.has_value());
  EXPECT_FALSE(root.is_spawn_point);

  const auto& child = scene->nodes[1];
  EXPECT_EQ(child.name, "Root/Child");
  EXPECT_EQ(child.parent_index, 0U);
  EXPECT_FLOAT_EQ(child.translation.y, 1.0F);
  ASSERT_TRUE(child.mesh_path.has_value());
  EXPECT_EQ(*child.mesh_path, "Root/Child");

  const auto& spawn = scene->nodes[2];
  EXPECT_EQ(spawn.name, "Root/Spawn");
  EXPECT_EQ(spawn.parent_index, 0U);
  EXPECT_TRUE(spawn.is_spawn_point);
  EXPECT_FLOAT_EQ(spawn.translation.x, 5.0F);
  EXPECT_FLOAT_EQ(spawn.translation.z, 5.0F);

  const auto mesh = pack->ResolveMesh("Root/Child");
  EXPECT_TRUE(mesh.has_value());
}

// Exercises augusta_assets' own texture-blob encode/resolve seam
// directly (WritePack/Pack::Load/ResolveTexture) - issue #49 - without
// going through Cook()/DirectXTex, which CooksFixtureTextureAndLoadsItBack
// below covers for the cooker side.
TEST_F(AssetPipelineTest, EncodesAndResolvesTextureBlob) {
  const auto pack_path = MakePackPath("augusta_asset_pipeline_test_texture_blob.pack");
  const auto keys = augusta::assets::GenerateEd25519KeyPair();

  const std::vector<std::byte> dds_bytes = {std::byte{0x44}, std::byte{0x44}, std::byte{0x53},
                                            std::byte{0x20}, std::byte{0xAB}, std::byte{0xCD}};
  const augusta::assets::TextureData texture{.dds_bytes = dds_bytes, .format = augusta::assets::TextureFormat::kBC5};

  const auto blob = augusta::assets::EncodeTextureBlob(texture);
  ASSERT_TRUE(blob.has_value());

  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kTexture, .path = "Tex", .data = *blob},
  };
  const auto written = augusta::assets::WritePack(pack_path, entries, keys.private_key);
  ASSERT_TRUE(written.has_value());

  auto pack = Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto resolved = pack->ResolveTexture("Tex");
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->format, augusta::assets::TextureFormat::kBC5);
  EXPECT_EQ(resolved->dds_bytes, dds_bytes);
}

// texture_fixture.usda's Mat/DiffuseTexture is a UsdUVTexture reading
// texture_fixture.png (an 8x8 solid-color PNG fixture image), bound to
// TestMesh's UsdPreviewSurface diffuseColor. Cook() must resolve and
// BC7-compress it (issue #49/ADR-0017), not skip it.
TEST_F(AssetPipelineTest, CooksFixtureTextureAndLoadsItBack) {
  const auto pack_path = MakePackPath("augusta_asset_pipeline_test_texture.pack");
  const auto keys = augusta::assets::GenerateEd25519KeyPair();

  const auto report = Cook(FixturePath("texture_fixture.usda"), pack_path, keys.private_key);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->mesh_count, 1U);
  EXPECT_EQ(report->texture_count, 1U);

  auto pack = Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto texture = pack->ResolveTexture("TestMesh/Mat/DiffuseTexture");
  ASSERT_TRUE(texture.has_value());
  EXPECT_EQ(texture->format, augusta::assets::TextureFormat::kBC7);

  // Not exact byte equality (BC7 output isn't a stable byte-for-byte
  // invariant, per issue #49's own acceptance criteria) - just a
  // plausible size: bigger than an empty/truncated blob, smaller than
  // the uncompressed 8x8x4-byte source would be as a sanity ceiling.
  EXPECT_GT(texture->dds_bytes.size(), 0U);
  EXPECT_LT(texture->dds_bytes.size(), 8U * 8U * 4U);
}

TEST_F(AssetPipelineTest, RejectsNonTriangularTopology) {
  const auto pack_path = MakePackPath("augusta_asset_pipeline_test_bad_topology.pack");
  const auto keys = augusta::assets::GenerateEd25519KeyPair();

  const auto report = Cook(FixturePath("bad_topology_fixture.usda"), pack_path, keys.private_key);
  ASSERT_FALSE(report.has_value());
  EXPECT_EQ(report.error().code, CookError::kUnsupportedTopology);
}

TEST_F(AssetPipelineTest, RejectsNegativeIndex) {
  const auto pack_path = MakePackPath("augusta_asset_pipeline_test_negative_index.pack");
  const auto keys = augusta::assets::GenerateEd25519KeyPair();

  const auto report = Cook(FixturePath("negative_index_fixture.usda"), pack_path, keys.private_key);
  ASSERT_FALSE(report.has_value());
  EXPECT_EQ(report.error().code, CookError::kNegativeIndex);
}

TEST_F(AssetPipelineTest, RejectsOutOfRangeIndex) {
  const auto pack_path = MakePackPath("augusta_asset_pipeline_test_out_of_range_index.pack");
  const auto keys = augusta::assets::GenerateEd25519KeyPair();

  const auto report = Cook(FixturePath("out_of_range_index_fixture.usda"), pack_path, keys.private_key);
  ASSERT_FALSE(report.has_value());
  EXPECT_EQ(report.error().code, CookError::kIndexOutOfRange);
}

TEST_F(AssetPipelineTest, RejectsInconsistentTopology) {
  const auto pack_path = MakePackPath("augusta_asset_pipeline_test_inconsistent_topology.pack");
  const auto keys = augusta::assets::GenerateEd25519KeyPair();

  const auto report = Cook(FixturePath("inconsistent_topology_fixture.usda"), pack_path, keys.private_key);
  ASSERT_FALSE(report.has_value());
  EXPECT_EQ(report.error().code, CookError::kInconsistentTopology);
}

TEST_F(AssetPipelineTest, WritePackRejectsDuplicatePaths) {
  augusta::assets::MeshData mesh;
  mesh.points = {Vec3(0.0F, 0.0F, 0.0F), Vec3(1.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F)};
  mesh.indices = {0, 1, 2};
  const auto blob = augusta::assets::EncodeMeshBlob(mesh);
  ASSERT_TRUE(blob.has_value());

  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{augusta::assets::AssetType::kMesh, "Dup", *blob},
      augusta::assets::AssetEntry{augusta::assets::AssetType::kMesh, "Dup", *blob},
  };

  const auto keys = augusta::assets::GenerateEd25519KeyPair();
  const auto pack_path = MakePackPath("augusta_asset_pipeline_test_dup.pack");
  const auto result = augusta::assets::WritePack(pack_path, entries, keys.private_key);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), augusta::assets::WriteError::kDuplicatePath);
}

// The remaining tests exercise Pack::Load's fail-closed parsing directly,
// by mutating the bytes of an otherwise validly cooked-and-signed pack -
// every case here is one ParsePackHeader (or the top-level size check)
// rejects before Load() ever computes/verifies the trailer, so none of
// these need a re-signed file to behave deterministically.
class PackLoadNegativeTest : public AssetPipelineTest {
 protected:
  void SetUp() override {
    keys_ = augusta::assets::GenerateEd25519KeyPair();
    const auto valid_path = MakePackPath("augusta_asset_pipeline_test_valid_source.pack");
    const auto report = Cook(FixturePath("mesh_fixture.usda"), valid_path, keys_.private_key);
    ASSERT_TRUE(report.has_value());
    valid_bytes_ = ReadFileBytes(valid_path);
    ASSERT_FALSE(valid_bytes_.empty());
  }

  Ed25519KeyPair keys_;
  std::vector<std::byte> valid_bytes_;
};

TEST_F(PackLoadNegativeTest, RejectsBadMagic) {
  auto bytes = valid_bytes_;
  bytes[0] = std::byte{'X'};
  const auto path = MakePackPath("augusta_asset_pipeline_test_bad_magic.pack");
  WriteFileBytes(path, bytes);

  const auto pack = Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kBadMagic);
}

TEST_F(PackLoadNegativeTest, RejectsUnsupportedVersion) {
  auto bytes = valid_bytes_;
  // Version is the u32 right after the 4-byte magic, little-endian.
  bytes[4] = std::byte{99};
  const auto path = MakePackPath("augusta_asset_pipeline_test_bad_version.pack");
  WriteFileBytes(path, bytes);

  const auto pack = Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kUnsupportedVersion);
}

TEST_F(PackLoadNegativeTest, RejectsFileTooShortForHeaderAndTrailer) {
  const std::vector<std::byte> bytes(valid_bytes_.begin(), valid_bytes_.begin() + 10);
  const auto path = MakePackPath("augusta_asset_pipeline_test_truncated_tiny.pack");
  WriteFileBytes(path, bytes);

  const auto pack = Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kTruncated);
}

TEST_F(PackLoadNegativeTest, RejectsIndexOffsetPastTruncatedContent) {
  // Short enough that the header's own (unchanged) index_offset now
  // claims to point past this file's much smaller hashed_length.
  const std::vector<std::byte> bytes(valid_bytes_.begin(), valid_bytes_.begin() + 40);
  const auto path = MakePackPath("augusta_asset_pipeline_test_truncated_mid.pack");
  WriteFileBytes(path, bytes);

  const auto pack = Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kTruncated);
}

TEST_F(PackLoadNegativeTest, RejectsCorruptedContentAsHashMismatch) {
  auto bytes = valid_bytes_;
  // Flip a byte inside the data section (right after the fixed-size
  // header) - the trailer's stored hash still reflects the original
  // content, so this must be caught as a mismatch rather than silently
  // loading corrupted mesh data.
  bytes[30] ^= std::byte{0xFF};
  const auto path = MakePackPath("augusta_asset_pipeline_test_corrupted.pack");
  WriteFileBytes(path, bytes);

  const auto pack = Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kHashMismatch);
}

TEST_F(PackLoadNegativeTest, RejectsWrongPublicKeyAsSignatureInvalid) {
  const auto path = MakePackPath("augusta_asset_pipeline_test_wrong_key.pack");
  WriteFileBytes(path, valid_bytes_);

  const auto other_keys = augusta::assets::GenerateEd25519KeyPair();
  const auto pack = Pack::Load(path, other_keys.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kSignatureInvalid);
}

}  // namespace
