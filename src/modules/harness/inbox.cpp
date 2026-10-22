#include "inbox.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/body_part.h"
#include "augusta/harness.h"
#include "augusta/link_types.h"
#include "augusta/logging.h"
#include "augusta/parameters.h"
#include "augusta/uuid.h"
#include "match_players.h"

namespace augusta::harness {

namespace {

// The two below are the only writers of the view's Authoritative States, so
// authoritative is always the last of recent_authoritative.

// Forgets the newest Authoritative State and the recent ones.
void ForgetStates(ServerView& view) {
  view.authoritative.reset();
  view.recent_authoritative.clear();
}

// Keeps state as the newest, and the last of the recent ones, the oldest of
// which goes once kRecentStatesKept are held.
void KeepState(ServerView& view, AuthoritativeState state) {
  if (view.recent_authoritative.size() == kRecentStatesKept) {
    view.recent_authoritative.erase(view.recent_authoritative.begin());
  }
  view.recent_authoritative.push_back(state);
  view.authoritative = std::move(state);
}

}  // namespace

void Inbox::Receive(link::ServerMessage message) {
  std::visit(
      [this]<typename Message>(Message&& said) {
        using Said = std::remove_cvref_t<Message>;
        if constexpr (std::is_same_v<Said, Admission>) {
          OnJoinAccepted(said);
        } else if constexpr (std::is_same_v<Said, JoinRefusal>) {
          OnJoinRefused(said);
        } else if constexpr (std::is_same_v<Said, AuthoritativeState>) {
          OnAuthoritativeState(std::forward<Message>(said));
        } else if constexpr (std::is_same_v<Said, Shot>) {
          OnShot(said);
        } else if constexpr (std::is_same_v<Said, HitConfirmation>) {
          OnHitConfirmation(said);
        } else if constexpr (std::is_same_v<Said, Death>) {
          OnDeath(said);
        } else if constexpr (std::is_same_v<Said, Lobby>) {
          OnLobby(std::forward<Message>(said));
        } else if constexpr (std::is_same_v<Said, MatchStart>) {
          OnMatchStart(std::forward<Message>(said));
        } else if constexpr (std::is_same_v<Said, MatchEnd>) {
          OnMatchEnd(said);
        } else if constexpr (std::is_same_v<Said, ReplayView>) {
          OnReplayView(std::forward<Message>(said));
        } else {
          // A Replay list, which a replay server sends only a client that asked for it.
          LOG_WARN_LIMITED(drop_warnings_, "subsystem=harness event=dropped reason=\"replay list unasked for\"");
        }
      },
      std::move(message));
}

std::shared_ptr<const ServerView> Inbox::View() const {
  const std::scoped_lock lock(view_mutex_);
  return view_;
}

std::vector<Shot> Inbox::TakeShots(std::uint32_t matches_started) { return shots_.Take(matches_started); }

std::vector<HitConfirmation> Inbox::TakeHitConfirmations(std::uint32_t matches_started) {
  return hit_confirmations_.Take(matches_started);
}

std::vector<Death> Inbox::TakeDeaths(std::uint32_t matches_started) { return deaths_.Take(matches_started); }

// A server whose tick rate or parameters the simulation cannot run on (a rate
// of zero would be divided by) is not a usable one: the message is dropped, as
// a malformed one is, and the client stays unadmitted.
void Inbox::OnJoinAccepted(const Admission& accepted) {
  if (!parameters::IsValidTickRate(accepted.tick_rate_hz)) {
    LOG_WARN_LIMITED(drop_warnings_, "subsystem=harness event=dropped reason=\"invalid tick rate\" tick_rate_hz={}",
                     accepted.tick_rate_hz);
    return;
  }
  if (const auto valid = parameters::Validate(accepted.parameters); !valid) {
    LOG_WARN_LIMITED(drop_warnings_, "subsystem=harness event=dropped reason=\"invalid parameters\" parameter={}",
                     valid.error().path);
    return;
  }
  Publish([&](ServerView& next) { next.accepted = accepted; });
  LOG_INFO("subsystem=harness event=joined session={} character={} tick_rate_hz={}", accepted.session,
           accepted.character, accepted.tick_rate_hz);
}

void Inbox::OnJoinRefused(JoinRefusal reason) {
  Publish([&](ServerView& next) { next.refusal = reason; });
  LOG_INFO("subsystem=harness event=join_refused reason=\"{}\"", DescribeJoinRefusal(reason));
}

void Inbox::OnLobby(Lobby lobby) {
  LOG_INFO("subsystem=harness event=lobby version={} players={}", lobby.version, lobby.roster.size());
  Publish([&](ServerView& next) { next.lobby = std::move(lobby); });
}

// A Match start that leaves this client out is not one it can play: dropped,
// as a malformed message is. A Replay viewer's leaves it out: it plays no one.
void Inbox::OnMatchStart(MatchStart start) {
  const std::shared_ptr<const ServerView> current = View();
  if (!current->accepted.has_value() || (!viewer_ && !IsInMatch(start, current->accepted->session))) {
    LOG_WARN_LIMITED(drop_warnings_, "subsystem=harness event=dropped reason=\"match start without this client\"");
    return;
  }
  const std::size_t players = start.players.size();
  // Before the start's line, which is the Match's.
  if (on_match_) {
    on_match_(start.match);
  }
  Publish([&](ServerView& next) {
    next.match_start = std::move(start);
    ++next.matches_started;
    next.in_match = true;
    next.match_end.reset();
    ForgetStates(next);
    next.dead.clear();
    next.replay_view.reset();
  });
  LOG_INFO("subsystem=harness event=match_started players={}", players);
}

void Inbox::OnMatchEnd(const MatchEnd& end) {
  Publish([&](ServerView& next) {
    next.in_match = false;
    ForgetStates(next);
    next.replay_view.reset();
    next.match_end = end;
  });
  // The fight's last events, the Deaths that ended the match among them, are
  // still taken: they stay under this match, which a view takes until the next
  // starts.
  if (end.winner.has_value()) {
    LOG_INFO("subsystem=harness event=match_ended winner={}", *end.winner);
  } else {
    LOG_INFO("subsystem=harness event=match_ended winner=draw");
  }
  // After the line above, which is still the Match's.
  if (on_match_) {
    on_match_(uuid::kNil);
  }
}

// Keeps shot for TakeShots if it is of the match in progress. One fired on
// the tick a match ended arrives after Match end, which is routine; one of a
// body not in the match is a server's mistake.
void Inbox::OnShot(const Shot& shot) {
  const std::shared_ptr<const ServerView> current = View();
  if (!current->in_match) {
    LOG_TRACE("subsystem=harness event=dropped tick={} reason=\"shot outside a match\"", shot.tick);
    return;
  }
  if (!IsInMatch(*current->match_start, shot.shooter)) {
    LOG_WARN_LIMITED(drop_warnings_,
                     "subsystem=harness event=dropped tick={} reason=\"shot names a body not in the match\"",
                     shot.tick);
    return;
  }
  shots_.Add(current->matches_started, shot);
}

// Keeps hit for TakeHitConfirmations if it is of the match in progress, as OnShot does a Shot.
void Inbox::OnHitConfirmation(const HitConfirmation& hit) {
  const std::shared_ptr<const ServerView> current = View();
  if (!current->in_match) {
    LOG_TRACE("subsystem=harness event=dropped reason=\"hit confirmation outside a match\"");
    return;
  }
  if (!IsInMatch(*current->match_start, hit.target)) {
    LOG_WARN_LIMITED(drop_warnings_,
                     "subsystem=harness event=dropped reason=\"hit confirmation names a body not in the match\"");
    return;
  }
  hit_confirmations_.Add(current->matches_started, hit);
}

// Keeps death for TakeDeaths, and its victim as dead for the rest of the
// match, if it is of the match in progress, as OnShot does a Shot. Both its
// victim and its killer are players of the match: one who has left it since
// is still in its Match start.
void Inbox::OnDeath(const Death& death) {
  const std::shared_ptr<const ServerView> current = View();
  if (!current->in_match) {
    LOG_TRACE("subsystem=harness event=dropped reason=\"death outside a match\"");
    return;
  }
  if (!IsInMatch(*current->match_start, death.victim) || !IsInMatch(*current->match_start, death.killer)) {
    LOG_WARN_LIMITED(drop_warnings_, "subsystem=harness event=dropped reason=\"death names a body not in the match\"");
    return;
  }
  Publish([&](ServerView& next) { next.dead.push_back(death.victim); });
  deaths_.Add(current->matches_started, death);
  LOG_INFO("subsystem=harness event=death victim={} killer={} part={}", std::to_underlying(death.victim),
           std::to_underlying(death.killer), body_part::NameOf(death.part));
}

// Keeps state if it is newer than the one held (unreliable delivery can
// reorder) and belongs to the match in progress: unreliable, it can arrive
// before Match start or after Match end.
void Inbox::OnAuthoritativeState(AuthoritativeState state) {
  const std::shared_ptr<const ServerView> current = View();
  if (!current->in_match) {
    LOG_TRACE("subsystem=harness event=dropped tick={} reason=\"state outside a match\"", state.tick);
    return;
  }
  const auto in_match = [&](const EntityBody& body) { return IsInMatch(*current->match_start, body.entity); };
  if (!std::ranges::all_of(state.bodies, in_match)) {
    LOG_WARN_LIMITED(drop_warnings_,
                     "subsystem=harness event=dropped tick={} reason=\"state names a body not in the match\"",
                     state.tick);
    return;
  }
  if (current->authoritative.has_value() && state.tick <= current->authoritative->tick) {
    return;
  }
  Publish([&](ServerView& next) { KeepState(next, std::move(state)); });
}

// Keeps view if it is newer than the one held and of the Replay in progress,
// as OnAuthoritativeState keeps a state: it travels as unreliably. A player's
// client is sent none.
void Inbox::OnReplayView(ReplayView view) {
  const std::shared_ptr<const ServerView> current = View();
  if (!viewer_ || !current->in_match) {
    LOG_TRACE("subsystem=harness event=dropped tick={} reason=\"replay view outside a replay\"", view.tick);
    return;
  }
  const auto in_match = [&](const PlayerView& player) { return IsInMatch(*current->match_start, player.entity); };
  if (!std::ranges::all_of(view.players, in_match)) {
    LOG_WARN_LIMITED(drop_warnings_,
                     "subsystem=harness event=dropped tick={} reason=\"replay view names a body not in the match\"",
                     view.tick);
    return;
  }
  if (current->replay_view.has_value() && view.tick <= current->replay_view->tick) {
    return;
  }
  Publish([&](ServerView& next) { next.replay_view = std::move(view); });
}

}  // namespace augusta::harness
