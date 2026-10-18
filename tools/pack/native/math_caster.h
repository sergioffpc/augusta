#ifndef AUGUSTA_PACK_MATH_CASTER_H_
#define AUGUSTA_PACK_MATH_CASTER_H_

#include <array>
#include <cstddef>
#include <span>

#include <pybind11/pybind11.h>

#include "augusta/math.h"

/// \file
/// pybind11's conversions of augusta::math's Vec3 and Quat, so the pack types
/// the cooker fills (pack_encoder.h) take and give plain Python tuples: a Vec3
/// is (x, y, z), a Quat (x, y, z, w), the order the pack stores a rotation in
/// (ADR-0032). Any sequence of that many numbers converts in.
namespace augusta::pack {

/// Loads source, a sequence of exactly components.size() numbers, into
/// components; false, with components unspecified, if it is not one.
inline bool LoadFloats(pybind11::handle source, bool convert, std::span<float> components) {
  if (!pybind11::isinstance<pybind11::sequence>(source) || pybind11::isinstance<pybind11::str>(source)) {
    return false;
  }
  const auto sequence = pybind11::reinterpret_borrow<pybind11::sequence>(source);
  if (sequence.size() != components.size()) {
    return false;
  }
  for (std::size_t i = 0; i < components.size(); ++i) {
    const pybind11::object item = sequence[i];
    pybind11::detail::make_caster<float> component;
    if (!component.load(item, convert)) {
      return false;
    }
    components[i] = pybind11::detail::cast_op<float>(component);
  }
  return true;
}

}  // namespace augusta::pack

namespace pybind11::detail {

template <>
struct type_caster<augusta::math::Vec3> {
 public:
  PYBIND11_TYPE_CASTER(augusta::math::Vec3, const_name("tuple[float, float, float]"));

  // load and cast: the names pybind11 calls a caster by.
  bool load(handle source, bool convert) {  // NOLINT(readability-identifier-naming)
    std::array<float, 3> components{};
    if (!augusta::pack::LoadFloats(source, convert, components)) {
      return false;
    }
    value = augusta::math::Vec3(components[0], components[1], components[2]);
    return true;
  }

  static handle cast(  // NOLINT(readability-identifier-naming)
      const augusta::math::Vec3& vec, return_value_policy /*policy*/, handle /*parent*/) {
    return pybind11::make_tuple(vec.x, vec.y, vec.z).release();
  }
};

template <>
struct type_caster<augusta::math::Quat> {
 public:
  PYBIND11_TYPE_CASTER(augusta::math::Quat, const_name("tuple[float, float, float, float]"));

  // load and cast: the names pybind11 calls a caster by.
  bool load(handle source, bool convert) {  // NOLINT(readability-identifier-naming)
    std::array<float, 4> components{};
    if (!augusta::pack::LoadFloats(source, convert, components)) {
      return false;
    }
    value.x = components[0];
    value.y = components[1];
    value.z = components[2];
    value.w = components[3];
    return true;
  }

  static handle cast(  // NOLINT(readability-identifier-naming)
      const augusta::math::Quat& quat, return_value_policy /*policy*/, handle /*parent*/) {
    return pybind11::make_tuple(quat.x, quat.y, quat.z, quat.w).release();
  }
};

}  // namespace pybind11::detail

#endif  // AUGUSTA_PACK_MATH_CASTER_H_
