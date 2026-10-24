#include "augusta/correction_tally.h"

#include <algorithm>
#include <cstdint>

#include "augusta/math.h"
#include "augusta/prediction.h"

namespace augusta::prediction {

void CorrectionTally::RecordTick(const State& state, bool counts) {
  // Reconciliation makes at most one jump per tick, so the change in the
  // running total is that tick's jump.
  const float jump = math::Length(state.total_correction - last_total_correction_);
  last_total_correction_ = state.total_correction;
  const std::uint32_t rifle = state.rifle_corrections - last_rifle_corrections_;
  last_rifle_corrections_ = state.rifle_corrections;
  if (!counts) {
    return;
  }
  totals_.rifle_count += rifle;
  if (jump > 0.0F) {
    ++totals_.count;
    totals_.last_m = jump;
    totals_.largest_m = std::max(totals_.largest_m, jump);
  }
}

}  // namespace augusta::prediction
