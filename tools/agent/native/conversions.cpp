#include "conversions.h"

#include <string>

#include <boost/uuid/uuid_io.hpp>

#include "augusta/harness.h"
#include "augusta/math.h"

namespace augusta::agent::native {

using augusta::harness::SessionId;
using augusta::math::Vec3;

Vec3Tuple ToTuple(const Vec3& vector) { return {vector.x, vector.y, vector.z}; }

Vec3 ToVec3(const Vec3Tuple& tuple) { return {std::get<0>(tuple), std::get<1>(tuple), std::get<2>(tuple)}; }

std::string Text(const SessionId& session) { return boost::uuids::to_string(session); }

}  // namespace augusta::agent::native
