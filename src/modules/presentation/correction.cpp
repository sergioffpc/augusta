#include "augusta/correction.h"

#include <cmath>

#include "augusta/math.h"

namespace augusta::presentation {

math::Vec3 Correction::Update(const math::Vec3& total_correction, float delta_time) {
  // The first frame has nothing shown before it to slide from.
  const math::Vec3 jump = has_seen_ ? total_correction - seen_total_ : math::Vec3{};
  seen_total_ = total_correction;
  has_seen_ = true;

  // What was hidden fades for the time that has passed, and the jump that came
  // with this frame is hidden in full: the body is shown where it was.
  offset_ *= std::exp(-delta_time / kFadeTimeConstant);
  if (math::Length(jump) >= kSnapDistance) {
    offset_ = math::Vec3{};
  } else {
    offset_ -= jump;
  }
  return offset_;
}

}  // namespace augusta::presentation
