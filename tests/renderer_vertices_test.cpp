#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/renderer.h"
#include "vertices.h"

namespace {

using augusta::math::Vec3;
using augusta::renderer::BuildEffectVertices;
using augusta::renderer::BuildFlatShadedVertices;
using augusta::renderer::BuildOverlayVertices;
using augusta::renderer::BuildRemoteVertices;
using augusta::renderer::Camera;
using augusta::renderer::CharacterVertices;
using augusta::renderer::CombatEffects;
using augusta::renderer::Overlay;
using augusta::renderer::RemotePlayer;
using augusta::renderer::Scene;
using augusta::renderer::SceneMesh;
using augusta::renderer::Tracer;
using augusta::renderer::Vertex;

constexpr float kTolerance = 1e-5F;

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

// A unit square in the XY plane, facing +Z, as two counter-clockwise triangles.
SceneMesh Square(const Vec3& color) {
  return {.positions = {Vec3(0.0F, 0.0F, 0.0F), Vec3(1.0F, 0.0F, 0.0F), Vec3(1.0F, 1.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F)},
          .indices = {0, 1, 2, 0, 2, 3},
          .color = color};
}

TEST(FlatShadedVerticesTest, ExpandsEveryTriangleIntoItsOwnFaceNormalAndMeshColor) {
  const Vec3 color(0.1F, 0.2F, 0.3F);
  const std::vector<Vertex> vertices = BuildFlatShadedVertices(Scene{.meshes = {Square(color)}, .camera = {}});

  ASSERT_EQ(vertices.size(), 6U);
  const std::vector<Vec3> corners{Vec3(0.0F, 0.0F, 0.0F), Vec3(1.0F, 0.0F, 0.0F), Vec3(1.0F, 1.0F, 0.0F),
                                  Vec3(0.0F, 0.0F, 0.0F), Vec3(1.0F, 1.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F)};
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    SCOPED_TRACE(i);
    ExpectNear(vertices[i].position, corners[i]);
    ExpectNear(vertices[i].normal, Vec3(0.0F, 0.0F, 1.0F));
    ExpectNear(vertices[i].color, color);
  }
}

TEST(FlatShadedVerticesTest, ConcatenatesEveryMeshInOrder) {
  const std::vector<Vertex> vertices = BuildFlatShadedVertices(
      Scene{.meshes = {Square(Vec3(1.0F, 0.0F, 0.0F)), Square(Vec3(0.0F, 1.0F, 0.0F))}, .camera = {}});

  ASSERT_EQ(vertices.size(), 12U);
  ExpectNear(vertices[5].color, Vec3(1.0F, 0.0F, 0.0F));
  ExpectNear(vertices[6].color, Vec3(0.0F, 1.0F, 0.0F));
}

TEST(FlatShadedVerticesTest, ThrowsOnAnIndexPastItsMeshsPositions) {
  SceneMesh mesh = Square(Vec3(1.0F, 1.0F, 1.0F));
  mesh.indices.back() = 4;

  EXPECT_THROW(BuildFlatShadedVertices(Scene{.meshes = {mesh}, .camera = {}}), std::runtime_error);
}

// One vertex pointing down -Z, the way a character faces at yaw 0, with its
// normal the same way.
CharacterVertices OneVertexCharacter() {
  return {
      {"soldier",
       {Vertex{
           .position = Vec3(0.0F, 2.0F, -1.0F), .normal = Vec3(0.0F, 0.0F, -1.0F), .color = Vec3(0.5F, 0.5F, 0.5F)}}}};
}

TEST(RemoteVerticesTest, TurnsByYawAndPlacesAtPositionInItsOwnColor) {
  const std::vector<RemotePlayer> players{{.position = Vec3(10.0F, 0.0F, 3.0F),
                                           .yaw = std::numbers::pi_v<float> / 2.0F,
                                           .color = Vec3(0.9F, 0.1F, 0.1F),
                                           .character = "soldier"}};

  const std::vector<Vertex> vertices = BuildRemoteVertices(players, OneVertexCharacter());

  // A quarter turn left takes -Z to -X.
  ASSERT_EQ(vertices.size(), 1U);
  ExpectNear(vertices[0].position, Vec3(9.0F, 2.0F, 3.0F));
  ExpectNear(vertices[0].normal, Vec3(-1.0F, 0.0F, 0.0F));
  ExpectNear(vertices[0].color, Vec3(0.9F, 0.1F, 0.1F));
}

TEST(RemoteVerticesTest, SkipsAPlayerWhoseCharacterHasNoMesh) {
  const std::vector<RemotePlayer> players{{.position = Vec3(1.0F, 0.0F, 0.0F), .character = "unknown"},
                                          {.position = Vec3(5.0F, 0.0F, 0.0F), .character = "soldier"}};

  const std::vector<Vertex> vertices = BuildRemoteVertices(players, OneVertexCharacter());

  ASSERT_EQ(vertices.size(), 1U);
  ExpectNear(vertices[0].position, Vec3(5.0F, 2.0F, -1.0F));
}

// A tracer along -Z seen from above is widened only across both its own line
// and the line to the eye: along X, off whichever end the vertex is at.
void ExpectWidenedAlongX(const Tracer& tracer, const Vertex& vertex) {
  const Vec3 end = std::abs(vertex.position.z - tracer.head.z) < kTolerance ? tracer.head : tracer.tail;
  const Vec3 offset = vertex.position - end;
  EXPECT_NEAR(offset.y, 0.0F, kTolerance);
  EXPECT_NEAR(offset.z, 0.0F, kTolerance);
  EXPECT_GT(std::abs(offset.x), 0.0F);
}

TEST(EffectVerticesTest, TracerIsAQuadAcrossItsLineFacingTheCamera) {
  const Tracer tracer{.head = Vec3(0.0F, 0.0F, -10.0F), .tail = Vec3(0.0F, 0.0F, -5.0F)};
  const Camera camera{.position = Vec3(0.0F, 3.0F, 0.0F)};

  const std::vector<Vertex> vertices =
      BuildEffectVertices(CombatEffects{.tracers = {tracer}, .impacts = {}, .muzzle_flashes = {}}, camera);

  ASSERT_EQ(vertices.size(), 6U);
  for (const Vertex& vertex : vertices) {
    ExpectWidenedAlongX(tracer, vertex);
  }
  // So it lies flat in y = 0, facing the eye above it.
  const Vec3 face =
      augusta::math::Cross(vertices[1].position - vertices[0].position, vertices[2].position - vertices[0].position);
  EXPECT_NEAR(face.x, 0.0F, kTolerance);
  EXPECT_NEAR(face.z, 0.0F, kTolerance);
  EXPECT_GT(std::abs(face.y), 0.0F);
}

TEST(EffectVerticesTest, TracerGlowsAtItsHeadAndFadesToNothingAtItsTail) {
  const Tracer tracer{.head = Vec3(0.0F, 0.0F, -10.0F), .tail = Vec3(0.0F, 0.0F, -5.0F)};

  const std::vector<Vertex> vertices =
      BuildEffectVertices(CombatEffects{.tracers = {tracer}, .impacts = {}, .muzzle_flashes = {}}, Camera{});

  for (const Vertex& vertex : vertices) {
    const bool at_head = std::abs(vertex.position.z - tracer.head.z) < kTolerance;
    EXPECT_EQ(at_head, vertex.color != Vec3()) << "z " << vertex.position.z;
  }
}

TEST(EffectVerticesTest, GlowsAreDiamondsOfFourTriangles) {
  const CombatEffects effects{.tracers = {},
                              .impacts = {{.position = Vec3(1.0F, 2.0F, 3.0F)}},
                              .muzzle_flashes = {{.position = Vec3(4.0F, 5.0F, 6.0F)}}};

  EXPECT_EQ(BuildEffectVertices(effects, Camera{}).size(), 2U * 4U * 3U);
}

// Vertices back from clip space to pixels off the frame's center.
std::vector<Vec3> ToPixels(std::span<const Vertex> vertices, std::uint32_t width, std::uint32_t height) {
  std::vector<Vec3> pixels;
  for (const Vertex& vertex : vertices) {
    pixels.emplace_back(vertex.position.x * static_cast<float>(width) / 2.0F,
                        vertex.position.y * static_cast<float>(height) / 2.0F, 0.0F);
  }
  return pixels;
}

TEST(OverlayVerticesTest, NothingShowsByDefault) { EXPECT_TRUE(BuildOverlayVertices(Overlay{}, 1920, 1080).empty()); }

TEST(OverlayVerticesTest, CrosshairIsFourBarsAroundTheCenterOnTheAxes) {
  const std::vector<Vertex> vertices = BuildOverlayVertices(Overlay{.crosshair = true}, 1920, 1080);

  ASSERT_EQ(vertices.size(), 4U * 6U);
  for (const Vec3& pixel : ToPixels(vertices, 1920, 1080)) {
    // Each bar runs along one axis and is thin across it, leaving the center clear.
    const float along = std::max(std::abs(pixel.x), std::abs(pixel.y));
    const float across = std::min(std::abs(pixel.x), std::abs(pixel.y));
    EXPECT_GT(along, across);
    EXPECT_GT(along, 1.0F);
  }
}

TEST(OverlayVerticesTest, KeepsItsSizeInPixelsWhateverTheClientAreaSize) {
  const Overlay overlay{.crosshair = true, .hit_marker = true};

  const std::vector<Vec3> small = ToPixels(BuildOverlayVertices(overlay, 640, 480), 640, 480);
  const std::vector<Vec3> large = ToPixels(BuildOverlayVertices(overlay, 2560, 1440), 2560, 1440);

  ASSERT_EQ(small.size(), large.size());
  for (std::size_t i = 0; i < small.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_NEAR(small[i].x, large[i].x, 1e-3F);
    EXPECT_NEAR(small[i].y, large[i].y, 1e-3F);
  }
}

TEST(OverlayVerticesTest, HitMarkerIsFourDiagonalBarsInItsOwnColor) {
  const std::vector<Vertex> crosshair = BuildOverlayVertices(Overlay{.crosshair = true}, 1920, 1080);
  const std::vector<Vertex> hit_marker = BuildOverlayVertices(Overlay{.hit_marker = true}, 1920, 1080);

  ASSERT_EQ(hit_marker.size(), 4U * 6U);
  EXPECT_NE(hit_marker.front().color, crosshair.front().color);
  for (const Vec3& pixel : ToPixels(hit_marker, 1920, 1080)) {
    // Off both axes, in every quadrant's diagonal.
    EXPECT_GT(std::abs(pixel.x), 1.0F);
    EXPECT_GT(std::abs(pixel.y), 1.0F);
  }
}

}  // namespace
