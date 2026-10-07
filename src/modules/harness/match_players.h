#ifndef AUGUSTA_HARNESS_MATCH_PLAYERS_H_
#define AUGUSTA_HARNESS_MATCH_PLAYERS_H_

#include <algorithm>
#include <optional>

#include "augusta/harness.h"

/// \file
/// Who a Match start names, by session or by body: what ServerView and Inbox
/// check a player, or a message about one, against. Private to the harness
/// module.
namespace augusta::harness {

inline bool IsInMatch(const MatchStart& start, SessionId session) {
  return std::ranges::any_of(start.players, [&](const MatchPlayer& player) { return player.session == session; });
}

inline bool IsInMatch(const MatchStart& start, EntityId entity) {
  return std::ranges::any_of(start.players, [&](const MatchPlayer& player) { return player.entity == entity; });
}

/// The body session's player controls in start; nullopt if it is not in it.
inline std::optional<EntityId> EntityOf(const MatchStart& start, SessionId session) {
  for (const MatchPlayer& player : start.players) {
    if (player.session == session) {
      return player.entity;
    }
  }
  return std::nullopt;
}

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_MATCH_PLAYERS_H_
