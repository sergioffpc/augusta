#include "correction.h"

#include <cmath>

#include "augusta/math.h"

namespace augusta::presentation {

namespace {

bool Snaps(const math::Vec3& jump) { return math::Length(jump) >= kSnapDistance; }

}  // namespace

math::Vec3 Correction::Update(const math::Vec3& previous_total, const math::Vec3& latest_total, float fraction,
                              float delta_time) {
  // A tick that snaps is shown as if blended all the way in from its first
  // frame: what the blend has yet to add of it is added here, and it is never
  // seen growing by the fractions the blend shows, which alone would slide.
  const math::Vec3 blended = math::Lerp(previous_total, latest_total, fraction);
  const bool snaps = Snaps(latest_total - previous_total);
  const math::Vec3 shown = snaps ? latest_total : blended;
  return Hide(shown, delta_time, snaps) + (shown - blended);
}

math::Vec3 Correction::Update(const math::Vec3& total_correction, float delta_time) {
  return Hide(total_correction, delta_time, false);
}

math::Vec3 Correction::Hide(const math::Vec3& total_correction, float delta_time, bool snaps) {
  // The first frame has nothing shown before it to slide from.
  const math::Vec3 jump = has_seen_ ? total_correction - seen_total_ : math::Vec3{};
  seen_total_ = total_correction;
  has_seen_ = true;

  // What was hidden fades for the time that has passed, and the jump that came
  // with this frame is hidden in full: the body is shown where it was.
  offset_ *= std::exp(-delta_time / kFadeTimeConstant);
  if (snaps || Snaps(jump)) {
    offset_ = math::Vec3{};
  } else {
    offset_ -= jump;
  }
  return offset_;
}

}  // namespace augusta::presentation
