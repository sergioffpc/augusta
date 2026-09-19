#ifndef AUGUSTA_MATH_H_
#define AUGUSTA_MATH_H_

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

// augusta::math is a thin facade over GLM (https://github.com/g-truc/glm):
// it re-exports the one type and handful of operations module interfaces
// actually need, under this project's own name and naming convention, so
// other modules depend on augusta::math rather than on GLM directly - the
// facade is the seam, not a reimplementation. GLM does all the actual
// arithmetic; this module adds no logic beyond the zero-vector guard on
// Normalize documented below.
namespace augusta::math {

// A 3D vector in engine units (1 unit = 1 meter, see ARCHITECTURE.md §8).
// Used both as a point (position) and a free vector (direction, velocity)
// depending on context; this type does not distinguish between the two.
//
// An alias for glm::vec3, so it is transparently also a valid glm::vec3
// anywhere one is expected: GLM's own constructors and arithmetic
// operators (+, -, scalar *, ==, ...) apply directly and are not
// redeclared here.
using Vec3 = glm::vec3;

// A rotation, as a unit quaternion. Alias for glm::quat - see Vec3's
// comment on why this module aliases rather than wraps.
using Quat = glm::quat;

// A 4x4 transform matrix (column-major, matching GLM/HLSL convention).
// Alias for glm::mat4 - see Vec3's comment on why this module aliases
// rather than wraps.
using Mat4 = glm::mat4;

// Composes a translation/rotation/scale into a single local-to-parent
// transform matrix, applied in that order (scale first, then rotate,
// then translate) - the standard TRS composition every scene-graph node
// (ADR-0032) uses to place itself relative to its parent.
inline Mat4 ToMat4(const Vec3& translation, const Quat& rotation, const Vec3& scale) {
  return glm::translate(Mat4(1.0F), translation) * glm::mat4_cast(rotation) * glm::scale(Mat4(1.0F), scale);
}

// The dot (scalar) product of lhs and rhs. Returns the cosine of the angle
// between them, scaled by both their lengths.
inline float Dot(const Vec3& lhs, const Vec3& rhs) { return glm::dot(lhs, rhs); }

// The cross product of lhs and rhs: a vector perpendicular to both,
// following the right-hand rule, with magnitude equal to the area of the
// parallelogram lhs and rhs span.
inline Vec3 Cross(const Vec3& lhs, const Vec3& rhs) { return glm::cross(lhs, rhs); }

// The Euclidean length (magnitude) of vec.
inline float Length(const Vec3& vec) { return glm::length(vec); }

// Returns vec scaled to unit length. Unlike glm::normalize, does not
// invoke undefined behavior on the zero vector: returns vec unchanged
// (i.e. the zero vector) instead of dividing by zero - callers that need
// a fallback direction must check for this case themselves.
inline Vec3 Normalize(const Vec3& vec) { return Length(vec) > 0.0F ? glm::normalize(vec) : vec; }

// The inverse of transform: maps back what transform mapped. transform must
// be invertible (no zero scale) - GLM does not check.
inline Mat4 Inverse(const Mat4& transform) { return glm::inverse(transform); }

// Applies transform to point (translation included, unlike for a free vector).
inline Vec3 TransformPoint(const Mat4& transform, const Vec3& point) { return {transform * glm::vec4(point, 1.0F)}; }

// The translation part of transform.
inline Vec3 TranslationOf(const Mat4& transform) { return {transform[3]}; }

// The rotation part of transform, as a unit quaternion. Assumes transform has
// no shear; a uniform or non-uniform scale is discarded.
inline Quat RotationOf(const Mat4& transform) {
  // NOLINTBEGIN(readability-identifier-length) - x/y/z are the basis axes' own notation.
  const Vec3 x = Normalize(Vec3(transform[0]));
  const Vec3 y = Normalize(Vec3(transform[1]));
  const Vec3 z = Normalize(Vec3(transform[2]));
  // NOLINTEND(readability-identifier-length)
  return glm::normalize(glm::quat_cast(glm::mat3(x, y, z)));
}

}  // namespace augusta::math

#endif  // AUGUSTA_MATH_H_
