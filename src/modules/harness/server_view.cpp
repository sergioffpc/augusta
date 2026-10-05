#include <algorithm>
#include <optional>

#include "augusta/harness.h"
#include "match_players.h"

namespace augusta::harness {

Phase ServerView::GetPhase() const {
  if (in_match) {
    return Phase::kMatch;
  }
  return accepted.has_value() ? Phase::kLobby : Phase::kNotAdmitted;
}

std::optional<EntityId> ServerView::OwnEntity() const {
  if (!accepted.has_value() || !match_start.has_value()) {
    return std::nullopt;
  }
  return EntityOf(*match_start, accepted->session);
}

// A Death and an update at zero health each say it is not, and either can
// arrive first: the one is reliable, the other can overtake it.
bool ServerView::OwnAlive() const {
  const std::optional<EntityId> own = OwnEntity();
  if (!in_match || !own.has_value()) {
    return false;
  }
  if (authoritative.has_value() && authoritative->health <= 0.0F) {
    return false;
  }
  return !std::ranges::contains(dead, *own);
}

}  // namespace augusta::harness
