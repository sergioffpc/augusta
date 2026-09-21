#include "augusta/reconciliation.h"

namespace augusta::prediction {

void History::Record(std::uint32_t sequence, const physics::MovementInput& command, const Predicted& predicted) {
  entries_.push_back(Entry{.sequence = sequence, .command = command, .predicted = predicted});
  if (entries_.size() > kMaxHistory) {
    entries_.pop_front();
  }
}

std::optional<Predicted> History::Acknowledge(std::uint32_t sequence) {
  while (!entries_.empty() && entries_.front().sequence < sequence) {
    entries_.pop_front();
  }
  if (entries_.empty() || entries_.front().sequence != sequence) {
    return std::nullopt;
  }
  const Predicted predicted = entries_.front().predicted;
  entries_.pop_front();
  return predicted;
}

void History::Replay(const Step& step) {
  for (Entry& entry : entries_) {
    entry.predicted = step(entry.command);
  }
}

}  // namespace augusta::prediction
