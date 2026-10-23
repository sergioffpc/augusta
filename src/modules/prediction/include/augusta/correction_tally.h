#ifndef AUGUSTA_CORRECTION_TALLY_H_
#define AUGUSTA_CORRECTION_TALLY_H_

#include <cstdint>

#include "augusta/math.h"
#include "augusta/prediction.h"

/// \file
/// How often and how far Reconciliation (ADR-0004) moved the local player's
/// body, and how often it put the rifle somewhere other than predicted, added
/// up from the running totals of the Prediction States a reader sees. The one
/// definition of a correction: harness::NetcodeTally reports it for the Agents
/// and the netcode tests, and the client's Network Debug panel shows it.
/// Counts only; rates over time and the bounds they are held to are the
/// reader's.
namespace augusta::prediction {

/// The corrections a CorrectionTally counted.
struct Corrections {
  /// The counted ticks on which Reconciliation moved the body.
  std::uint32_t count = 0;
  /// How far the most recent of those moved it, in metres; 0 before the first.
  float last_m = 0.0F;
  /// The farthest any one of those moved it, in metres.
  float largest_m = 0.0F;
  /// The times, on counted ticks, Reconciliation put the rifle at a server's
  /// state that differed from the one predicted.
  std::uint32_t rifle = 0;
};

/// Adds up Corrections tick by tick from State::total_correction and
/// State::rifle_corrections. Not thread-safe: one reader feeds it.
class CorrectionTally {
 public:
  /// One tick's state. A tick that does not count (one outside a Match)
  /// follows the running totals but adds nothing, so what happened before it
  /// is never counted after it.
  void RecordTick(const State& state, bool counts);

  [[nodiscard]] Corrections Totals() const { return totals_; }

 private:
  Corrections totals_;
  math::Vec3 last_total_correction_{};
  std::uint32_t last_rifle_corrections_ = 0;
};

}  // namespace augusta::prediction

#endif  // AUGUSTA_CORRECTION_TALLY_H_
