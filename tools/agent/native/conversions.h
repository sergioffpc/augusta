#ifndef AUGUSTA_AGENT_CONVERSIONS_H_
#define AUGUSTA_AGENT_CONVERSIONS_H_

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "augusta/harness.h"
#include "augusta/math.h"

/// \file
/// The values the Agents' Python module (module.cpp) hands a script in place
/// of the engine's own, shared by its bindings (bindings.h): vectors as
/// tuples, IDs as numbers and strings, sequences as tuples, each a copy.
namespace augusta::agent::native {

using Vec3Tuple = std::tuple<float, float, float>;

Vec3Tuple ToTuple(const math::Vec3& vector);

math::Vec3 ToVec3(const Vec3Tuple& tuple);

/// An Entity ID as the number the script sees.
template <typename Id>
std::uint32_t Number(Id id) {
  return static_cast<std::uint32_t>(id);
}

/// A Session ID as the string the script sees: its canonical form, which a
/// script only compares.
std::string Text(const harness::SessionId& session);

template <typename T>
pybind11::tuple ToPyTuple(const std::vector<T>& items) {
  return pybind11::tuple(pybind11::cast(items));
}

}  // namespace augusta::agent::native

#endif  // AUGUSTA_AGENT_CONVERSIONS_H_
