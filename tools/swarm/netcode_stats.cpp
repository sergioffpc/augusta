#include "netcode_stats.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "augusta/math.h"
#include "augusta/prediction.h"

namespace augusta::swarm {

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
  // Reconciliation makes at most one jump per tick, so the change in the
  // running total is that tick's jump (as harness::Runner's heartbeat reads it).
  const float jump = math::Length(state.total_correction - last_total_correction_);
  last_total_correction_ = state.total_correction;
  // The rounds by their running total too: outside a Match the state is the
  // last one predicted, a round it fired included, again.
  const std::uint32_t rounds = state.total_rounds_fired - last_total_rounds_fired_;
  last_total_rounds_fired_ = state.total_rounds_fired;
  if (!in_match) {
    return;
  }
  ++stats_.match_ticks;
  stats_.rounds_fired += rounds;
  if (jump > 0.0F) {
    ++stats_.corrections;
    stats_.largest_correction_m = std::max(stats_.largest_correction_m, jump);
  }
}

void NetcodeTally::RecordHitConfirmations(std::size_t count) {
  stats_.hit_confirmations += static_cast<std::uint32_t>(count);
}

}  // namespace augusta::swarm
