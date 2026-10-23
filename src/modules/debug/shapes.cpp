#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>
#include <vector>

#include "augusta/debug_shapes.h"
#include "augusta/math.h"

namespace augusta::debug {

namespace {

// A shape's colour and alpha, which every vertex it is drawn with carries.
struct Paint {
  math::Vec3 color;
  float alpha;
};

// Where a shape is drawn, by whether it shows through walls.
Primitives& PrimitivesFor(Tessellation& tessellation, bool through_walls) {
  return through_walls ? tessellation.through_walls : tessellation.depth_tested;
}

void AddLine(std::vector<Vertex>& lines, const Paint& paint, const math::Vec3& from, const math::Vec3& to) {
  lines.push_back({.position = from, .color = paint.color, .alpha = paint.alpha});
  lines.push_back({.position = to, .color = paint.color, .alpha = paint.alpha});
}

void AddTriangle(std::vector<Vertex>& triangles, const Paint& paint, const math::Vec3& first, const math::Vec3& second,
                 const math::Vec3& third) {
  for (const math::Vec3& corner : {first, second, third}) {
    triangles.push_back({.position = corner, .color = paint.color, .alpha = paint.alpha});
  }
}

// A quad as two triangles, its corners given in order around it.
void AddQuad(std::vector<Vertex>& triangles, const Paint& paint, const std::array<math::Vec3, 4>& corners) {
  AddTriangle(triangles, paint, corners[0], corners[1], corners[2]);
  AddTriangle(triangles, paint, corners[0], corners[2], corners[3]);
}

// A box's corners: corner i is at the negative or positive half extent along
// the box's X, Y and Z axes by its bits 0, 1 and 2.
constexpr std::size_t kBoxCorners = 8;
using BoxCorners = std::array<math::Vec3, kBoxCorners>;

// The corners joined by each of a box's edges, as indices into BoxCorners.
constexpr std::array<std::pair<std::size_t, std::size_t>, 12> kBoxEdges{
    {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};

// The corners of each of a box's faces, in order around it, as indices into
// BoxCorners.
constexpr std::array<std::array<std::size_t, 4>, 6> kBoxFaces{
    {{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}}};

BoxCorners CornersOf(const math::Vec3& center, const math::Vec3& half_extents, const math::Quat& rotation) {
  BoxCorners corners;
  for (std::size_t i = 0; i < corners.size(); ++i) {
    const math::Vec3 local{(i & 1U) != 0 ? half_extents.x : -half_extents.x,
                           (i & 2U) != 0 ? half_extents.y : -half_extents.y,
                           (i & 4U) != 0 ? half_extents.z : -half_extents.z};
    corners[i] = center + (rotation * local);
  }
  return corners;
}

void AddBox(Primitives& primitives, const BoxCorners& corners, Style style, const Paint& paint) {
  if (style == Style::kWireframe) {
    for (const auto& [from, to] : kBoxEdges) {
      AddLine(primitives.lines, paint, corners[from], corners[to]);
    }
    return;
  }
  for (const auto& face : kBoxFaces) {
    AddQuad(primitives.triangles, paint, {corners[face[0]], corners[face[1]], corners[face[2]], corners[face[3]]});
  }
}

// A curved shape's fixed segment counts: rings from pole to pole of a sphere,
// and points round each ring. The stacks are even so the equator is a ring,
// and the slices a multiple of four so a wireframe's meridians are two great
// circles.
constexpr std::size_t kStacks = 12;
constexpr std::size_t kSlices = 24;

using Ring = std::array<math::Vec3, kSlices>;

// A unit axis and two unit directions across it, square to it and each other.
struct Frame {
  math::Vec3 axis;
  math::Vec3 across;
  math::Vec3 across_too;
};

// The frame about the direction from from to to, or about Y where they meet.
Frame FrameBetween(const math::Vec3& from, const math::Vec3& to) {
  const math::Vec3 along = to - from;
  const math::Vec3 axis = math::Length(along) > 0.0F ? math::Normalize(along) : math::Vec3{0.0F, 1.0F, 0.0F};
  // Any direction not along the axis gives a direction across it.
  const math::Vec3 off_axis = std::abs(axis.y) < 0.9F ? math::Vec3{0.0F, 1.0F, 0.0F} : math::Vec3{1.0F, 0.0F, 0.0F};
  const math::Vec3 across = math::Normalize(math::Cross(axis, off_axis));
  return {.axis = axis, .across = across, .across_too = math::Cross(axis, across)};
}

// The ring stack steps from the pole along frame's axis, radius from center.
Ring RingAt(const math::Vec3& center, const Frame& frame, float radius, std::size_t stack) {
  const float polar = std::numbers::pi_v<float> * static_cast<float>(stack) / static_cast<float>(kStacks);
  Ring ring;
  for (std::size_t slice = 0; slice < kSlices; ++slice) {
    const float azimuth = 2.0F * std::numbers::pi_v<float> * static_cast<float>(slice) / static_cast<float>(kSlices);
    const math::Vec3 across = (frame.across * std::cos(azimuth)) + (frame.across_too * std::sin(azimuth));
    ring[slice] = center + (radius * ((frame.axis * std::cos(polar)) + (across * std::sin(polar))));
  }
  return ring;
}

// A capsule's surface, as rings from the pole of its cap round to to the
// pole of its cap round from; the two equators, which the band between the
// caps joins, at equator and equator_too. A sphere is a capsule whose ends
// meet, with one equator.
struct Rings {
  std::vector<Ring> rings;
  std::size_t equator;
  std::size_t equator_too;
};

Rings RingsOf(const math::Vec3& from, const math::Vec3& to, float radius) {
  const Frame frame = FrameBetween(from, to);
  Rings rings{.rings = {}, .equator = kStacks / 2, .equator_too = kStacks / 2};
  for (std::size_t stack = 0; stack <= kStacks / 2; ++stack) {
    rings.rings.push_back(RingAt(to, frame, radius, stack));
  }
  // The cap round from starts on its own equator, unless that is to's.
  const bool ends_meet = from == to;
  if (!ends_meet) {
    rings.equator_too = rings.rings.size();
  }
  for (std::size_t stack = ends_meet ? (kStacks / 2) + 1 : kStacks / 2; stack <= kStacks; ++stack) {
    rings.rings.push_back(RingAt(from, frame, radius, stack));
  }
  return rings;
}

// A wireframe: the equators, and four meridians from pole to pole.
void AddOutline(std::vector<Vertex>& lines, const Paint& paint, const Rings& rings) {
  for (const std::size_t equator : {rings.equator, rings.equator_too}) {
    for (std::size_t slice = 0; slice < kSlices; ++slice) {
      AddLine(lines, paint, rings.rings[equator][slice], rings.rings[equator][(slice + 1) % kSlices]);
    }
    if (rings.equator_too == rings.equator) {
      break;
    }
  }
  for (std::size_t slice = 0; slice < kSlices; slice += kSlices / 4) {
    for (std::size_t ring = 0; ring + 1 < rings.rings.size(); ++ring) {
      AddLine(lines, paint, rings.rings[ring][slice], rings.rings[ring + 1][slice]);
    }
  }
}

// Filled: a quad between each two rings at each two slices, as one triangle
// at a pole, where one ring is a point.
void AddSurface(std::vector<Vertex>& triangles, const Paint& paint, const Rings& rings) {
  const std::size_t last = rings.rings.size() - 1;
  for (std::size_t ring = 0; ring < last; ++ring) {
    for (std::size_t slice = 0; slice < kSlices; ++slice) {
      const std::size_t next = (slice + 1) % kSlices;
      const math::Vec3& upper = rings.rings[ring][slice];
      const math::Vec3& upper_next = rings.rings[ring][next];
      const math::Vec3& lower = rings.rings[ring + 1][slice];
      const math::Vec3& lower_next = rings.rings[ring + 1][next];
      if (ring != 0) {
        AddTriangle(triangles, paint, upper, upper_next, lower_next);
      }
      if (ring + 1 != last) {
        AddTriangle(triangles, paint, upper, lower_next, lower);
      }
    }
  }
}

void AddCapsule(Primitives& primitives, const Rings& rings, Style style, const Paint& paint) {
  if (style == Style::kWireframe) {
    AddOutline(primitives.lines, paint, rings);
  } else {
    AddSurface(primitives.triangles, paint, rings);
  }
}

}  // namespace

Tessellation Tessellate(const Shapes& shapes) {
  Tessellation tessellation;
  for (const Segment& segment : shapes.segments) {
    AddLine(PrimitivesFor(tessellation, segment.through_walls).lines, {.color = segment.color, .alpha = segment.alpha},
            segment.from, segment.to);
  }
  for (const Triangle& triangle : shapes.triangles) {
    AddTriangle(PrimitivesFor(tessellation, triangle.through_walls).triangles,
                {.color = triangle.color, .alpha = triangle.alpha}, triangle.corners[0], triangle.corners[1],
                triangle.corners[2]);
  }
  for (const Box& box : shapes.boxes) {
    AddBox(PrimitivesFor(tessellation, box.through_walls), CornersOf(box.center, box.half_extents, box.rotation),
           box.style, {.color = box.color, .alpha = box.alpha});
  }
  for (const Sphere& sphere : shapes.spheres) {
    AddCapsule(PrimitivesFor(tessellation, sphere.through_walls), RingsOf(sphere.center, sphere.center, sphere.radius),
               sphere.style, {.color = sphere.color, .alpha = sphere.alpha});
  }
  for (const Capsule& capsule : shapes.capsules) {
    AddCapsule(PrimitivesFor(tessellation, capsule.through_walls), RingsOf(capsule.from, capsule.to, capsule.radius),
               capsule.style, {.color = capsule.color, .alpha = capsule.alpha});
  }
  for (const Voxel& voxel : shapes.voxels) {
    const float half_edge = voxel.edge / 2.0F;
    AddBox(PrimitivesFor(tessellation, voxel.through_walls),
           CornersOf(voxel.center, {half_edge, half_edge, half_edge}, math::Quat{1.0F, 0.0F, 0.0F, 0.0F}), voxel.style,
           {.color = voxel.color, .alpha = voxel.alpha});
  }
  return tessellation;
}

}  // namespace augusta::debug
