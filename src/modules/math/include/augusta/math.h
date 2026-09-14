#ifndef AUGUSTA_MATH_H_
#define AUGUSTA_MATH_H_

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

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

}  // namespace augusta::math

#endif  // AUGUSTA_MATH_H_
