#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/debug_kept_shapes.h"
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

using Edges = std::set<std::pair<std::size_t, std::size_t>>;

// A box's 12 edges, for corners indexed so that corner i is at the negative or
// positive half extent along X, Y and Z by its bits 0, 1 and 2: two corners
// share an edge when their indices differ in one bit.
const Edges kBoxEdges{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

// The corners of an axis-aligned box, indexed as kBoxEdges says.
std::array<math::Vec3, 8> AxisAlignedCorners(const math::Vec3& center, const math::Vec3& half_extents) {
  std::array<math::Vec3, 8> corners{};
  for (std::size_t i = 0; i < corners.size(); ++i) {
    corners[i] = center + math::Vec3{(i & 1U) != 0 ? half_extents.x : -half_extents.x,
                                     (i & 2U) != 0 ? half_extents.y : -half_extents.y,
                                     (i & 4U) != 0 ? half_extents.z : -half_extents.z};
  }
  return corners;
}

// The index of the corner of corners that point is at, if any.
std::optional<std::size_t> CornerAt(const std::array<math::Vec3, 8>& corners, const math::Vec3& point) {
  constexpr float kTolerance = 1e-4F;
  for (std::size_t i = 0; i < corners.size(); ++i) {
    if (math::Length(corners[i] - point) < kTolerance) {
      return i;
    }
  }
  return std::nullopt;
}

// The edges lines draws between corners, each as its two corners' indices,
// lower first; nullopt if a line ends away from every corner.
std::optional<Edges> EdgesBetween(const std::array<math::Vec3, 8>& corners, const Vertices& lines) {
  Edges edges;
  for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
    const std::optional<std::size_t> from = CornerAt(corners, lines[i].position);
    const std::optional<std::size_t> to = CornerAt(corners, lines[i + 1].position);
    if (!from.has_value() || !to.has_value()) {
      return std::nullopt;
    }
    edges.emplace(std::min(*from, *to), std::max(*from, *to));
  }
  return edges;
}

TEST(TessellateTest, DrawsAWireBoxAsItsTwelveEdgesAtItsCornersAfterRotation) {
  Shapes shapes;
  // A quarter turn about Y takes the box's X half extent onto Z and its Z
  // half extent onto X.
  shapes.boxes.push_back({.center = {10.0F, 0.0F, 0.0F},
                          .half_extents = {1.0F, 2.0F, 3.0F},
                          .rotation = math::Quat(std::sqrt(0.5F), 0.0F, std::sqrt(0.5F), 0.0F),
                          .color = kOrange,
                          .alpha = 1.0F,
                          .style = Style::kWireframe,
                          .through_walls = false});

  const Tessellation tessellation = Tessellate(shapes);

  const std::array<math::Vec3, 8> corners = AxisAlignedCorners({10.0F, 0.0F, 0.0F}, {3.0F, 2.0F, 1.0F});
  ASSERT_EQ(tessellation.depth_tested.lines.size(), 24U);
  EXPECT_EQ(EdgesBetween(corners, tessellation.depth_tested.lines), kBoxEdges);
  EXPECT_EQ(tessellation.depth_tested.lines[0].color, kOrange);
  EXPECT_TRUE(tessellation.depth_tested.triangles.empty());
}

// Whether triangles, three vertices each, cover the six faces of the box
// corners are at with two triangles each, every vertex at a corner.
bool CoversTheBoxFaces(const std::array<math::Vec3, 8>& corners, const Vertices& triangles) {
  // A face is the corners whose indices agree on one bit: bit b set or clear.
  std::array<int, 6> triangles_per_face{};
  for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
    std::array<std::size_t, 3> indices{};
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const std::optional<std::size_t> index = CornerAt(corners, triangles[i + corner].position);
      if (!index.has_value()) {
        return false;
      }
      indices[corner] = *index;
    }
    for (std::size_t bit = 0; bit < 3; ++bit) {
      const std::size_t mask = std::size_t{1} << bit;
      if ((indices[0] & mask) == (indices[1] & mask) && (indices[1] & mask) == (indices[2] & mask)) {
        ++triangles_per_face[(2 * bit) + ((indices[0] & mask) != 0 ? 1 : 0)];
      }
    }
  }
  return std::ranges::all_of(triangles_per_face, [](int count) { return count == 2; });
}

TEST(TessellateTest, DrawsAFilledBoxAsTwoTrianglesOnEachFace) {
  Shapes shapes;
  shapes.boxes.push_back({.center = {0.0F, 1.0F, 0.0F},
                          .half_extents = {0.5F, 1.0F, 0.25F},
                          .color = kTeal,
                          .alpha = 0.5F,
                          .style = Style::kFilled,
                          .through_walls = false});

  const Tessellation tessellation = Tessellate(shapes);

  ASSERT_EQ(tessellation.depth_tested.triangles.size(), 36U);
  EXPECT_TRUE(CoversTheBoxFaces(AxisAlignedCorners({0.0F, 1.0F, 0.0F}, {0.5F, 1.0F, 0.25F}),
                                tessellation.depth_tested.triangles));
  EXPECT_EQ(tessellation.depth_tested.triangles[0].alpha, 0.5F);
  EXPECT_TRUE(tessellation.depth_tested.lines.empty());
}

TEST(TessellateTest, DrawsAVoxelAsACubeOfItsEdgeOnItsCentre) {
  Shapes shapes;
  shapes.voxels.push_back({.center = {2.0F, 3.0F, 4.0F}, .edge = 0.5F, .style = Style::kWireframe});
  shapes.voxels.push_back({.center = {2.0F, 3.0F, 4.0F}, .edge = 0.5F, .style = Style::kFilled});

  const Tessellation tessellation = Tessellate(shapes);

  const std::array<math::Vec3, 8> cube = AxisAlignedCorners({2.0F, 3.0F, 4.0F}, {0.25F, 0.25F, 0.25F});
  ASSERT_EQ(tessellation.depth_tested.lines.size(), 24U);
  EXPECT_EQ(EdgesBetween(cube, tessellation.depth_tested.lines), kBoxEdges);
  ASSERT_EQ(tessellation.depth_tested.triangles.size(), 36U);
  EXPECT_TRUE(CoversTheBoxFaces(cube, tessellation.depth_tested.triangles));
}

// How far from its surface a shape's vertices may lie: curved shapes are
// tessellated in floats.
constexpr float kOnSurface = 1e-4F;

// The vertices of both a shape's lines and its triangles.
Vertices AllOf(const Primitives& primitives) {
  Vertices vertices = primitives.lines;
  vertices.insert(vertices.end(), primitives.triangles.begin(), primitives.triangles.end());
  return vertices;
}

// The distance from point to the segment between from and to.
float DistanceToSegment(const math::Vec3& point, const math::Vec3& from, const math::Vec3& to) {
  const math::Vec3 along = to - from;
  const float t = std::clamp(math::Dot(point - from, along) / math::Dot(along, along), 0.0F, 1.0F);
  return math::Length(point - (from + (along * t)));
}

// How far along direction from origin the farthest of vertices lies.
float ReachAlong(const Vertices& vertices, const math::Vec3& origin, const math::Vec3& direction) {
  float reach = -1e9F;
  for (const Vertex& vertex : vertices) {
    reach = std::max(reach, math::Dot(vertex.position - origin, direction));
  }
  return reach;
}

TEST(TessellateTest, DrawsASphereWithEveryVertexOnItsSurface) {
  constexpr math::Vec3 kCenter{1.0F, 2.0F, 3.0F};
  constexpr float kRadius = 0.75F;
  for (const Style style : {Style::kWireframe, Style::kFilled}) {
    Shapes shapes;
    shapes.spheres.push_back({.center = kCenter, .radius = kRadius, .style = style});

    const Tessellation tessellation = Tessellate(shapes);

    const Primitives& drawn = tessellation.depth_tested;
    EXPECT_TRUE(style == Style::kWireframe ? drawn.triangles.empty() : drawn.lines.empty());
    const Vertices vertices = AllOf(drawn);
    ASSERT_FALSE(vertices.empty());
    for (const Vertex& vertex : vertices) {
      EXPECT_NEAR(math::Length(vertex.position - kCenter), kRadius, kOnSurface);
    }
    // On the surface all round, not on one side of it.
    for (const math::Vec3& axis :
         {math::Vec3{1.0F, 0.0F, 0.0F}, math::Vec3{0.0F, 1.0F, 0.0F}, math::Vec3{0.0F, 0.0F, 1.0F}}) {
      EXPECT_GT(ReachAlong(vertices, kCenter, axis), 0.9F * kRadius);
      EXPECT_GT(ReachAlong(vertices, kCenter, -axis), 0.9F * kRadius);
    }
  }
}

TEST(TessellateTest, DrawsACapsuleWithEveryVertexOnItsSurface) {
  constexpr math::Vec3 kFrom{0.0F, 0.0F, 0.0F};
  constexpr math::Vec3 kTo{1.0F, 1.0F, 1.0F};
  constexpr float kRadius = 0.25F;
  for (const Style style : {Style::kWireframe, Style::kFilled}) {
    Shapes shapes;
    shapes.capsules.push_back({.from = kFrom, .to = kTo, .radius = kRadius, .style = style});

    const Tessellation tessellation = Tessellate(shapes);

    const Primitives& drawn = tessellation.depth_tested;
    EXPECT_TRUE(style == Style::kWireframe ? drawn.triangles.empty() : drawn.lines.empty());
    const Vertices vertices = AllOf(drawn);
    ASSERT_FALSE(vertices.empty());
    for (const Vertex& vertex : vertices) {
      EXPECT_NEAR(DistanceToSegment(vertex.position, kFrom, kTo), kRadius, kOnSurface);
    }
    // Capped at both ends, a radius past each.
    const math::Vec3 axis = math::Normalize(kTo - kFrom);
    EXPECT_NEAR(ReachAlong(vertices, kTo, axis), kRadius, kOnSurface);
    EXPECT_NEAR(ReachAlong(vertices, kFrom, -axis), kRadius, kOnSurface);
  }
}

TEST(TessellateTest, KeepsEachOfBoxesSpheresCapsulesAndVoxelsThatShowThroughWallsApartFromTheRest) {
  std::array<Shapes, 4> each_shape;
  each_shape[0].boxes.push_back({.half_extents = {1.0F, 1.0F, 1.0F}, .style = Style::kFilled, .through_walls = true});
  each_shape[1].spheres.push_back({.radius = 1.0F, .style = Style::kFilled, .through_walls = true});
  each_shape[2].capsules.push_back(
      {.to = {0.0F, 1.0F, 0.0F}, .radius = 1.0F, .style = Style::kFilled, .through_walls = true});
  each_shape[3].voxels.push_back({.style = Style::kFilled, .through_walls = true});

  for (const Shapes& shapes : each_shape) {
    const Tessellation tessellation = Tessellate(shapes);

    EXPECT_TRUE(tessellation.depth_tested.triangles.empty());
    EXPECT_FALSE(tessellation.through_walls.triangles.empty());
  }
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

using std::chrono::seconds;

const KeptShapes::Clock::time_point kStart{seconds{100}};

Shapes ASphereAt(float x) {
  Shapes shapes;
  shapes.spheres.push_back({.center = {x, 0.0F, 0.0F}, .radius = 1.0F});
  return shapes;
}

TEST(KeptShapesTest, ReturnsShapesKeptUntilATimeBeforeThatTime) {
  KeptShapes kept;
  Shapes shapes = ASphereAt(1.0F);
  shapes.segments.push_back(SegmentAt(2.0F, true));
  kept.Keep(shapes, kStart + seconds{3});

  EXPECT_EQ(kept.Alive(kStart + seconds{2}), shapes);
}

TEST(KeptShapesTest, ReturnsShapesKeptUntilDifferentTimesTogether) {
  KeptShapes kept;
  kept.Keep(ASphereAt(1.0F), kStart + seconds{3});
  kept.Keep(ASphereAt(2.0F), kStart + seconds{5});

  Shapes both = ASphereAt(1.0F);
  both.spheres.push_back(ASphereAt(2.0F).spheres[0]);
  EXPECT_EQ(kept.Alive(kStart + seconds{1}), both);
}

TEST(KeptShapesTest, ReturnsNothingOnceItsTimeComes) {
  KeptShapes kept;
  kept.Keep(ASphereAt(1.0F), kStart + seconds{3});

  EXPECT_EQ(kept.Alive(kStart + seconds{2}), ASphereAt(1.0F));
  EXPECT_EQ(kept.Alive(kStart + seconds{3}), Shapes{});
}

TEST(KeptShapesTest, DropsShapesWhoseTimeHasComeFromTheStore) {
  KeptShapes kept;
  kept.Keep(ASphereAt(1.0F), kStart + seconds{3});
  kept.Keep(ASphereAt(2.0F), kStart + seconds{5});

  EXPECT_EQ(kept.Alive(kStart + seconds{4}), ASphereAt(2.0F));
  // An earlier time no longer finds the first: it was dropped, not skipped.
  EXPECT_EQ(kept.Alive(kStart + seconds{1}), ASphereAt(2.0F));
}

}  // namespace
}  // namespace augusta::debug
