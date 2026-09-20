#include "augusta/reconciliation.h"

namespace augusta::prediction {

Correction ResolveCorrection(const physics::BodyState& predicted, const physics::BodyState& authoritative) {
  Correction correction;
  correction.error = math::Length(authoritative.position - predicted.position);
  correction.snapped = correction.error >= kSnapDistance;
  const float share = correction.snapped ? 1.0F : kBlendFactor;
  correction.position = (authoritative.position - predicted.position) * share;
  correction.velocity = (authoritative.velocity - predicted.velocity) * share;
  correction.stamina = (authoritative.stamina - predicted.stamina) * share;
  if (authoritative.stance != predicted.stance) {
    correction.stance = authoritative.stance;
  }
  return correction;
}

physics::BodyState Apply(const physics::BodyState& state, const Correction& correction) {
  physics::BodyState corrected = state;
  corrected.position += correction.position;
  corrected.velocity += correction.velocity;
  corrected.stamina += correction.stamina;
  if (correction.stance.has_value()) {
    corrected.stance = *correction.stance;
  }
  return corrected;
}

void History::Record(std::uint32_t sequence, const physics::BodyState& state) {
  entries_.push_back(Entry{.sequence = sequence, .state = state});
  if (entries_.size() > kMaxHistory) {
    entries_.pop_front();
  }
}

std::optional<physics::BodyState> History::Acknowledge(std::uint32_t sequence) {
  while (!entries_.empty() && entries_.front().sequence < sequence) {
    entries_.pop_front();
  }
  if (entries_.empty() || entries_.front().sequence != sequence) {
    return std::nullopt;
  }
  const physics::BodyState state = entries_.front().state;
  entries_.pop_front();
  return state;
}

void History::Shift(const Correction& correction) {
  Correction without_stance = correction;
  without_stance.stance.reset();
  for (Entry& entry : entries_) {
    entry.state = Apply(entry.state, without_stance);
  }
}

}  // namespace augusta::prediction
