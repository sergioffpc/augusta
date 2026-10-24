#ifndef AUGUSTA_DEBUG_SHAPES_H_
#define AUGUSTA_DEBUG_SHAPES_H_

#include <array>
#include <cstdint>
#include <vector>

#include "augusta/math.h"

/// \file
/// augusta::debug's Debug overlay shapes (ADR-0075): what an area's debug tool
/// asks to be drawn into the world, as values, and their tessellation into the
/// one-pixel lines and the triangles the Renderer draws. An area's module builds
/// a frame's Shapes from its state, the client Runtime hands them to the
/// Renderer (Renderer::SetDebugShapes), and the Renderer tessellates and draws
/// them, keeping nothing between frames. Free of any GPU API and of every area
/// module, and compiled only in a Development build (ADR-0074).
namespace augusta::debug {

/// A line between two points, such as one of PhysX's visualization lines,
/// which go in as they come. Always drawn as a line, one pixel wide.
struct Segment {
  /// World space (Y-up, right-handed, 1 unit = 1 m - ADR-0032), as every
  /// shape's points.
  math::Vec3 from{};
  math::Vec3 to{};
  /// Linear RGB in [0, 1], as every shape's.
  math::Vec3 color{1.0F, 1.0F, 1.0F};
  /// From 0, unseen, to 1, opaque, as every shape's: blended over what is
  /// behind it.
  float alpha = 1.0F;
  /// Drawn over what is in front of it, as a Hitbox behind cover is, rather
  /// than hidden behind it, as every shape's.
  bool through_walls = false;

  bool operator==(const Segment&) const = default;
};

/// A triangle, always filled, seen from both sides.
struct Triangle {
  std::array<math::Vec3, 3> corners{};
  math::Vec3 color{1.0F, 1.0F, 1.0F};
  float alpha = 1.0F;
  bool through_walls = false;

  bool operator==(const Triangle&) const = default;
};

/// How a box, a sphere, a capsule or a voxel is drawn: as one-pixel lines
/// along its outline, or as triangles over its surface.
enum class Style : std::uint8_t { kWireframe, kFilled };

/// An oriented box.
struct Box {
  math::Vec3 center{};
  /// Half the box's size along each of its own axes, in metres.
  math::Vec3 half_extents{};
  /// From the box's own axes to the world's.
  math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
  math::Vec3 color{1.0F, 1.0F, 1.0F};
  float alpha = 1.0F;
  Style style = Style::kWireframe;
  bool through_walls = false;

  bool operator==(const Box&) const = default;
};

/// A sphere, drawn with a fixed number of segments.
struct Sphere {
  math::Vec3 center{};
  /// In metres.
  float radius = 0.0F;
  math::Vec3 color{1.0F, 1.0F, 1.0F};
  float alpha = 1.0F;
  Style style = Style::kWireframe;
  bool through_walls = false;

  bool operator==(const Sphere&) const = default;
};

/// The points within radius of the segment between from and to, such as a
/// collider's capsule, drawn with a fixed number of segments.
struct Capsule {
  /// The centres of its two hemispherical caps.
  math::Vec3 from{};
  math::Vec3 to{};
  /// In metres.
  float radius = 0.0F;
  math::Vec3 color{1.0F, 1.0F, 1.0F};
  float alpha = 1.0F;
  Style style = Style::kWireframe;
  bool through_walls = false;

  bool operator==(const Capsule&) const = default;
};

/// An axis-aligned cube on a grid, such as one of a Smoke screen's voxels.
struct Voxel {
  math::Vec3 center{};
  /// The cube's edge, in metres.
  float edge = 1.0F;
  math::Vec3 color{1.0F, 1.0F, 1.0F};
  float alpha = 1.0F;
  Style style = Style::kWireframe;
  bool through_walls = false;

  bool operator==(const Voxel&) const = default;
};

/// A frame's shapes, which a producer appends to: drawn for the one frame they
/// are handed to the Renderer for.
struct Shapes {
  std::vector<Segment> segments;
  std::vector<Triangle> triangles;
  std::vector<Box> boxes;
  std::vector<Sphere> spheres;
  std::vector<Capsule> capsules;
  std::vector<Voxel> voxels;

  bool operator==(const Shapes&) const = default;
};

/// Appends every shape of shapes to to, kind by kind, after what to holds.
void Append(Shapes& to, const Shapes& shapes);

/// One corner of a drawn line or triangle, in world space, with its shape's
/// colour and alpha.
struct Vertex {
  math::Vec3 position{};
  math::Vec3 color{};
  float alpha = 1.0F;

  bool operator==(const Vertex&) const = default;
};

/// Lines, two vertices each, and triangles, three each: kind by kind in the
/// order Shapes lists them, and each kind's shapes in the order they were
/// appended.
struct Primitives {
  std::vector<Vertex> lines;
  std::vector<Vertex> triangles;
};

/// Shapes as drawn: those hidden behind what is in front of them apart from
/// those that show through walls, which are drawn without the depth test.
struct Tessellation {
  Primitives depth_tested;
  Primitives through_walls;
};

Tessellation Tessellate(const Shapes& shapes);

}  // namespace augusta::debug

#endif  // AUGUSTA_DEBUG_SHAPES_H_
