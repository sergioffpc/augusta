#include <vector>

#include "augusta/debug_shapes.h"
#include "augusta/math.h"

namespace augusta::debug {

namespace {

// Where a shape is drawn, by whether it shows through walls.
Primitives& PrimitivesFor(Tessellation& tessellation, bool through_walls) {
  return through_walls ? tessellation.through_walls : tessellation.depth_tested;
}

}  // namespace

Tessellation Tessellate(const Shapes& shapes) {
  Tessellation tessellation;
  for (const Segment& segment : shapes.segments) {
    std::vector<Vertex>& lines = PrimitivesFor(tessellation, segment.through_walls).lines;
    lines.push_back({.position = segment.from, .color = segment.color, .alpha = segment.alpha});
    lines.push_back({.position = segment.to, .color = segment.color, .alpha = segment.alpha});
  }
  for (const Triangle& triangle : shapes.triangles) {
    std::vector<Vertex>& triangles = PrimitivesFor(tessellation, triangle.through_walls).triangles;
    for (const math::Vec3& corner : triangle.corners) {
      triangles.push_back({.position = corner, .color = triangle.color, .alpha = triangle.alpha});
    }
  }
  return tessellation;
}

}  // namespace augusta::debug
