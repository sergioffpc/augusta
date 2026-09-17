#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

#include "augusta/asset_cooking.h"
#include "augusta/assets.h"
#include "augusta/math.h"

// Walking skeleton (ROADMAP.md M2, issue #46): cooks a small fixture USD
// stage into an unsigned pack and loads it back, proving the pack format
// and the cook/load split work before any real optimization,
// compression, hashing, or signing exists (ADR-0030/ADR-0031).
namespace {

using augusta::asset_cooking::Cook;
using augusta::assets::Pack;
using augusta::math::Vec3;

TEST(AssetPipelineTest, CooksFixtureMeshAndLoadsItBack) {
  const std::filesystem::path stage_path =
      std::filesystem::path(AUGUSTA_ASSET_PIPELINE_FIXTURE_DIR) / "mesh_fixture.usda";
  const std::filesystem::path pack_path = std::filesystem::temp_directory_path() / "augusta_asset_pipeline_test.pack";

  const auto report = Cook(stage_path, pack_path);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->mesh_count, 1U);

  auto pack = Pack::Load(pack_path);
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

  std::filesystem::remove(pack_path);
}

}  // namespace
