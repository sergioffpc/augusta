#include "augusta/netcode_stats.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "augusta/correction_tally.h"
#include "augusta/prediction.h"

namespace augusta::harness {

NetcodeStats& NetcodeStats::operator+=(const NetcodeStats& other) {
  match_ticks += other.match_ticks;
  corrections += other.corrections;
  largest_correction_m = std::max(largest_correction_m, other.largest_correction_m);
  rounds_fired += other.rounds_fired;
  hit_confirmations += other.hit_confirmations;
  return *this;
}

float NetcodeStats::CorrectionRate() const {
  return match_ticks == 0 ? 0.0F : static_cast<float>(corrections) / static_cast<float>(match_ticks);
}

float NetcodeStats::HitRate() const {
  return rounds_fired == 0 ? 0.0F : static_cast<float>(hit_confirmations) / static_cast<float>(rounds_fired);
}

void NetcodeTally::RecordTick(const prediction::State& state, bool in_match) {
  corrections_.RecordTick(state, in_match);
  // The rounds by their running total: outside a Match the state is the last
  // one predicted, a round it fired included, again.
  const std::uint32_t rounds = state.total_rounds_fired - last_total_rounds_fired_;
  last_total_rounds_fired_ = state.total_rounds_fired;
  if (!in_match) {
    return;
  }
  ++stats_.match_ticks;
  stats_.rounds_fired += rounds;
}

void NetcodeTally::RecordHitConfirmations(std::size_t count) {
  stats_.hit_confirmations += static_cast<std::uint32_t>(count);
}

NetcodeStats NetcodeTally::Stats() const {
  const prediction::Corrections corrections = corrections_.Totals();
  NetcodeStats stats = stats_;
  stats.corrections = corrections.count;
  stats.largest_correction_m = corrections.largest_m;
  return stats;
}

}  // namespace augusta::harness
