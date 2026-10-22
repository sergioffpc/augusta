#include <vector>

#include <gtest/gtest.h>

#include "augusta/debug_shapes.h"
#include "augusta/math.h"

namespace augusta::debug {
namespace {

using Vertices = std::vector<Vertex>;

constexpr math::Vec3 kOrange{1.0F, 0.5F, 0.0F};
constexpr math::Vec3 kTeal{0.0F, 0.5F, 0.5F};

Segment SegmentAt(float y, bool through_walls) {
  return {
      .from = {0.0F, y, 0.0F}, .to = {1.0F, y, 0.0F}, .color = kOrange, .alpha = 1.0F, .through_walls = through_walls};
}

Triangle TriangleAt(float z, bool through_walls) {
  return {.corners = {{{0.0F, 0.0F, z}, {1.0F, 0.0F, z}, {0.0F, 1.0F, z}}},
          .color = kTeal,
          .alpha = 0.5F,
          .through_walls = through_walls};
}

TEST(TessellateTest, DrawsASegmentAsOneLineInItsColour) {
  Shapes shapes;
  shapes.segments.push_back(
      {.from = {0.0F, 0.0F, 0.0F}, .to = {1.0F, 2.0F, 3.0F}, .color = kOrange, .alpha = 0.25F, .through_walls = false});

  const Tessellation tessellation = Tessellate(shapes);

  EXPECT_EQ(tessellation.depth_tested.lines,
            (Vertices{{.position = {0.0F, 0.0F, 0.0F}, .color = kOrange, .alpha = 0.25F},
                      {.position = {1.0F, 2.0F, 3.0F}, .color = kOrange, .alpha = 0.25F}}));
  EXPECT_TRUE(tessellation.depth_tested.triangles.empty());
}

TEST(TessellateTest, DrawsATriangleAsOneTriangleInItsColour) {
  Shapes shapes;
  shapes.triangles.push_back(TriangleAt(-2.0F, false));

  const Tessellation tessellation = Tessellate(shapes);

  EXPECT_EQ(tessellation.depth_tested.triangles,
            (Vertices{{.position = {0.0F, 0.0F, -2.0F}, .color = kTeal, .alpha = 0.5F},
                      {.position = {1.0F, 0.0F, -2.0F}, .color = kTeal, .alpha = 0.5F},
                      {.position = {0.0F, 1.0F, -2.0F}, .color = kTeal, .alpha = 0.5F}}));
  EXPECT_TRUE(tessellation.depth_tested.lines.empty());
}

TEST(TessellateTest, DrawsNothingForNoShapes) {
  const Tessellation tessellation = Tessellate({});

  EXPECT_TRUE(tessellation.depth_tested.lines.empty());
  EXPECT_TRUE(tessellation.depth_tested.triangles.empty());
  EXPECT_TRUE(tessellation.through_walls.lines.empty());
  EXPECT_TRUE(tessellation.through_walls.triangles.empty());
}

TEST(TessellateTest, KeepsShapesThatShowThroughWallsApartFromTheRest) {
  Shapes shapes;
  shapes.segments = {SegmentAt(1.0F, true), SegmentAt(2.0F, false)};
  shapes.triangles = {TriangleAt(3.0F, false), TriangleAt(4.0F, true)};

  const Tessellation tessellation = Tessellate(shapes);

  ASSERT_EQ(tessellation.depth_tested.lines.size(), 2U);
  EXPECT_EQ(tessellation.depth_tested.lines[0].position.y, 2.0F);
  ASSERT_EQ(tessellation.through_walls.lines.size(), 2U);
  EXPECT_EQ(tessellation.through_walls.lines[0].position.y, 1.0F);
  ASSERT_EQ(tessellation.depth_tested.triangles.size(), 3U);
  EXPECT_EQ(tessellation.depth_tested.triangles[0].position.z, 3.0F);
  ASSERT_EQ(tessellation.through_walls.triangles.size(), 3U);
  EXPECT_EQ(tessellation.through_walls.triangles[0].position.z, 4.0F);
}

}  // namespace
}  // namespace augusta::debug
