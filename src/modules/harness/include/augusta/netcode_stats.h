#ifndef AUGUSTA_NETCODE_STATS_H_
#define AUGUSTA_NETCODE_STATS_H_

#include <cstddef>
#include <cstdint>

#include "augusta/math.h"
#include "augusta/prediction.h"

/// \file
/// What one player's prediction and fire came to over a run, from the
/// Prediction States its Session's Ticks left: how often and how far
/// reconciliation (ADR-0004) moved its body, and how many of its rounds the
/// server confirmed as hits (ADR-0044). What a test of the netcode under an
/// impaired link judges, and what a load test reports at the end of a run.
/// Counts only; the bounds they are held to are the caller's.
namespace augusta::harness {

/// One player's run, as its NetcodeTally added it up.
struct NetcodeStats {
  /// The ticks it predicted in a Match.
  std::uint32_t match_ticks = 0;
  /// Of those, the ticks on which reconciliation moved its body.
  std::uint32_t corrections = 0;
  /// The farthest any one of those moved it, in metres.
  float largest_correction_m = 0.0F;
  /// The rounds its rifle fired, as it predicted them.
  std::uint32_t rounds_fired = 0;
  /// The Hit confirmations the server sent it.
  std::uint32_t hit_confirmations = 0;

  /// Adds other's counts to these, as of several players together; the
  /// largest correction is the larger of the two.
  NetcodeStats& operator+=(const NetcodeStats& other);
  /// The share of match_ticks with a correction, 0 if there were none.
  [[nodiscard]] float CorrectionRate() const;
  /// The share of rounds_fired the server confirmed as hits, 0 if none were fired.
  [[nodiscard]] float HitRate() const;
};

/// Adds up a player's NetcodeStats, tick by tick. Not thread-safe:
/// the caller guards it when ticks and Hit confirmations come from two threads.
class NetcodeTally {
 public:
  /// One tick the player's Runner ran, which left state; in_match says whether
  /// it was played in a Match.
  void RecordTick(const prediction::State& state, bool in_match);
  /// The Hit confirmations the player was handed since the last call.
  void RecordHitConfirmations(std::size_t count);

  [[nodiscard]] NetcodeStats Stats() const { return stats_; }

 private:
  NetcodeStats stats_;
  math::Vec3 last_total_correction_{};
  std::uint32_t last_total_rounds_fired_ = 0;
};

}  // namespace augusta::harness

#endif  // AUGUSTA_NETCODE_STATS_H_
