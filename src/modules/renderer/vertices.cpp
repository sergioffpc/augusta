#include "vertices.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "augusta/math.h"
#include "augusta/renderer.h"

namespace augusta::renderer {

namespace {

// How the fight is drawn (SetCombatEffects): a tracer's half width, in meters,
// and the color its head glows with - its tail fades to nothing - and each
// glow's half size, in meters, and color at its center.
constexpr float kTracerHalfWidth = 0.015F;
constexpr math::Vec3 kTracerColor{1.0F, 0.75F, 0.35F};
constexpr float kMuzzleFlashHalfSize = 0.12F;
constexpr math::Vec3 kMuzzleFlashColor{1.0F, 0.7F, 0.3F};
constexpr float kImpactHalfSize = 0.08F;
constexpr math::Vec3 kImpactColor{0.9F, 0.8F, 0.6F};

// How the overlay is drawn (SetOverlay), in pixels: the crosshair's four bars
// start kCrosshairGapPixels off the frame's center and run kBarLengthPixels on;
// the hit marker's four, diagonal, start kHitMarkerGapPixels off it.
constexpr float kCrosshairGapPixels = 4.0F;
constexpr float kHitMarkerGapPixels = 7.0F;
constexpr float kBarLengthPixels = 8.0F;
constexpr float kBarThicknessPixels = 2.0F;
constexpr math::Vec3 kCrosshairColor{0.9F, 0.9F, 0.9F};
constexpr math::Vec3 kHitMarkerColor{1.0F, 0.25F, 0.2F};

Vertex Unlit(const math::Vec3& position, const math::Vec3& color) {
  return {.position = position, .normal = math::Vec3(), .color = color};
}

// Appends tracer as a ribbon from its tail to its head, turned about its own
// line to face eye, glowing at the head and fading to nothing at the tail.
void AppendTracer(std::vector<Vertex>& vertices, const Tracer& tracer, const math::Vec3& eye) {
  const math::Vec3 side = math::Normalize(math::Cross(tracer.head - tracer.tail, tracer.head - eye)) * kTracerHalfWidth;
  const Vertex head_left = Unlit(tracer.head - side, kTracerColor);
  const Vertex head_right = Unlit(tracer.head + side, kTracerColor);
  const Vertex tail_left = Unlit(tracer.tail - side, math::Vec3());
  const Vertex tail_right = Unlit(tracer.tail + side, math::Vec3());
  vertices.insert(vertices.end(), {tail_left, head_left, head_right, tail_left, head_right, tail_right});
}

// Appends glow as a diamond of half_size around its position, in the plane of
// right and up (the camera's, so it faces the camera), glowing color scaled by
// how much of it is left at its center and fading to nothing at its corners.
void AppendGlow(std::vector<Vertex>& vertices, const Glow& glow, const math::Vec3& right, const math::Vec3& up,
                float half_size, const math::Vec3& color) {
  const Vertex center = Unlit(glow.position, color * glow.fade);
  const std::array<math::Vec3, 4> corners{glow.position + (right * half_size), glow.position + (up * half_size),
                                          glow.position - (right * half_size), glow.position - (up * half_size)};
  for (std::size_t i = 0; i < corners.size(); ++i) {
    vertices.insert(vertices.end(),
                    {center, Unlit(corners[i], math::Vec3()), Unlit(corners[(i + 1) % corners.size()], math::Vec3())});
  }
}

// Appends a bar kBarThicknessPixels thick from from to to, both in pixels off
// the frame's center (+x right, +y up, z unused), in clip space: to_clip is
// the size of a pixel there.
void AppendBar(std::vector<Vertex>& vertices, const math::Vec3& from, const math::Vec3& to, const math::Vec3& to_clip,
               const math::Vec3& color) {
  const math::Vec3 along = math::Normalize(to - from);
  const math::Vec3 across = math::Vec3(-along.y, along.x, 0.0F) * (kBarThicknessPixels / 2.0F);
  const Vertex from_left = Unlit((from + across) * to_clip, color);
  const Vertex from_right = Unlit((from - across) * to_clip, color);
  const Vertex to_left = Unlit((to + across) * to_clip, color);
  const Vertex to_right = Unlit((to - across) * to_clip, color);
  vertices.insert(vertices.end(), {from_left, to_left, to_right, from_left, to_right, from_right});
}

// Four bars pointing out from the frame's center along each of directions
// (unit, in pixel space), starting gap pixels off it.
void AppendBars(std::vector<Vertex>& vertices, std::span<const math::Vec3> directions, float gap,
                const math::Vec3& to_clip, const math::Vec3& color) {
  for (const math::Vec3& direction : directions) {
    AppendBar(vertices, direction * gap, direction * (gap + kBarLengthPixels), to_clip, color);
  }
}

}  // namespace

std::vector<Vertex> BuildFlatShadedVertices(const Scene& scene) {
  std::vector<Vertex> vertices;
  for (const SceneMesh& mesh : scene.meshes) {
    for (const std::uint32_t index : mesh.indices) {
      if (index >= mesh.positions.size()) {
        throw std::runtime_error("scene mesh index out of range for its positions");
      }
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
      // NOLINTBEGIN(readability-identifier-length) - a/b/c are the triangle's own corner notation.
      const math::Vec3& a = mesh.positions[mesh.indices[i]];
      const math::Vec3& b = mesh.positions[mesh.indices[i + 1]];
      const math::Vec3& c = mesh.positions[mesh.indices[i + 2]];
      // NOLINTEND(readability-identifier-length)
      const math::Vec3 normal = math::Normalize(math::Cross(b - a, c - a));
      for (const math::Vec3* corner : {&a, &b, &c}) {
        vertices.push_back({.position = *corner, .normal = normal, .color = mesh.color});
      }
    }
  }
  return vertices;
}

std::vector<Vertex> BuildRemoteVertices(std::span<const RemotePlayer> remote_players,
                                        const CharacterVertices& character_vertices) {
  std::vector<Vertex> vertices;
  for (const RemotePlayer& remote : remote_players) {
    const auto found = character_vertices.find(remote.character);
    if (found == character_vertices.end()) {
      continue;
    }
    // About +Y, counter-clockwise seen from above: -Z turns toward -X as yaw grows.
    const float cos_yaw = std::cos(remote.yaw);
    const float sin_yaw = std::sin(remote.yaw);
    const auto turned = [cos_yaw, sin_yaw](const math::Vec3& v) {
      return math::Vec3((v.x * cos_yaw) + (v.z * sin_yaw), v.y, (v.z * cos_yaw) - (v.x * sin_yaw));
    };
    for (const Vertex& local_vertex : found->second) {
      vertices.push_back({
          .position = turned(local_vertex.position) + remote.position,
          .normal = turned(local_vertex.normal),
          .color = remote.color,
      });
    }
  }
  return vertices;
}

std::vector<Vertex> BuildEffectVertices(const CombatEffects& effects, const Camera& camera) {
  const math::Vec3 right = camera.rotation * math::Vec3(1.0F, 0.0F, 0.0F);
  const math::Vec3 up = camera.rotation * math::Vec3(0.0F, 1.0F, 0.0F);
  std::vector<Vertex> vertices;
  for (const Tracer& tracer : effects.tracers) {
    AppendTracer(vertices, tracer, camera.position);
  }
  for (const Glow& impact : effects.impacts) {
    AppendGlow(vertices, impact, right, up, kImpactHalfSize, kImpactColor);
  }
  for (const Glow& flash : effects.muzzle_flashes) {
    AppendGlow(vertices, flash, right, up, kMuzzleFlashHalfSize, kMuzzleFlashColor);
  }
  return vertices;
}

std::vector<Vertex> BuildOverlayVertices(const Overlay& overlay, std::uint32_t width, std::uint32_t height) {
  const math::Vec3 to_clip(2.0F / static_cast<float>(width), 2.0F / static_cast<float>(height), 0.0F);
  constexpr float kDiagonal = 0.70710678F;
  constexpr std::array<math::Vec3, 4> kAxes{math::Vec3(1.0F, 0.0F, 0.0F), math::Vec3(0.0F, 1.0F, 0.0F),
                                            math::Vec3(-1.0F, 0.0F, 0.0F), math::Vec3(0.0F, -1.0F, 0.0F)};
  constexpr std::array<math::Vec3, 4> kDiagonals{
      math::Vec3(kDiagonal, kDiagonal, 0.0F), math::Vec3(-kDiagonal, kDiagonal, 0.0F),
      math::Vec3(-kDiagonal, -kDiagonal, 0.0F), math::Vec3(kDiagonal, -kDiagonal, 0.0F)};
  std::vector<Vertex> vertices;
  if (overlay.crosshair) {
    AppendBars(vertices, kAxes, kCrosshairGapPixels, to_clip, kCrosshairColor);
  }
  if (overlay.hit_marker) {
    AppendBars(vertices, kDiagonals, kHitMarkerGapPixels, to_clip, kHitMarkerColor);
  }
  return vertices;
}

}  // namespace augusta::renderer
