#include "inbox.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/harness.h"
#include "augusta/harness_wire.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/protocol.h"
#include "match_players.h"

namespace augusta::harness {

namespace {

std::string_view BodyPartName(ballistics::BodyPart part) {
  switch (part) {
    case ballistics::BodyPart::kHead:
      return "head";
    case ballistics::BodyPart::kTorso:
      return "torso";
    case ballistics::BodyPart::kLimb:
      return "limb";
  }
  std::unreachable();
}

}  // namespace

void Inbox::Receive(const networking::Payload& payload) {
  const std::expected<protocol::MessageWire, protocol::DecodeError> decoded = protocol::Decode(payload);
  if (!decoded.has_value()) {
    LW_LIMITED(drop_warnings_, "subsystem=harness event=dropped bytes={} reason=\"{}\"", payload.size(),
               protocol::DescribeDecodeError(decoded.error()));
    return;
  }
  if (!TakeIn(*decoded)) {
    LW_LIMITED(drop_warnings_, "subsystem=harness event=dropped bytes={} reason=\"not a server message\"",
               payload.size());
  }
}

std::shared_ptr<const ServerView> Inbox::View() const { return view_.load(); }

std::vector<Shot> Inbox::TakeShots(std::uint32_t matches_started) { return shots_.Take(matches_started); }

std::vector<HitConfirmation> Inbox::TakeHitConfirmations(std::uint32_t matches_started) {
  return hit_confirmations_.Take(matches_started);
}

std::vector<Death> Inbox::TakeDeaths(std::uint32_t matches_started) { return deaths_.Take(matches_started); }

bool Inbox::TakeIn(const protocol::MessageWire& message) {
  if (const auto* accepted = std::get_if<protocol::JoinAcceptedWire>(&message)) {
    OnJoinAccepted(FromWire(*accepted));
  } else if (const auto* refused = std::get_if<protocol::JoinRefusedWire>(&message)) {
    OnJoinRefused(FromWire(refused->reason));
  } else if (const auto* state = std::get_if<protocol::AuthoritativeStateWire>(&message)) {
    OnAuthoritativeState(FromWire(*state));
  } else if (const auto* shot = std::get_if<protocol::ShotWire>(&message)) {
    OnShot(FromWire(*shot));
  } else if (const auto* hit = std::get_if<protocol::HitConfirmationWire>(&message)) {
    OnHitConfirmation(FromWire(*hit));
  } else if (const auto* death = std::get_if<protocol::DeathWire>(&message)) {
    OnDeath(FromWire(*death));
  } else if (const auto* lobby = std::get_if<protocol::LobbyWire>(&message)) {
    OnLobby(FromWire(*lobby));
  } else if (const auto* start = std::get_if<protocol::MatchStartWire>(&message)) {
    OnMatchStart(FromWire(*start));
  } else if (const auto* end = std::get_if<protocol::MatchEndWire>(&message)) {
    OnMatchEnd(FromWire(*end));
  } else {
    return false;
  }
  return true;
}

// A server whose tick rate or parameters the simulation cannot run on (a rate
// of zero would be divided by) is not a usable one: the message is dropped, as
// a malformed one is, and the client stays unadmitted.
void Inbox::OnJoinAccepted(const Admission& accepted) {
  if (!parameters::IsValidTickRate(accepted.tick_rate_hz)) {
    LW_LIMITED(drop_warnings_, "subsystem=harness event=dropped reason=\"invalid tick rate\" tick_rate_hz={}",
               accepted.tick_rate_hz);
    return;
  }
  if (const auto valid = parameters::Validate(accepted.parameters); !valid) {
    LW_LIMITED(drop_warnings_, "subsystem=harness event=dropped reason=\"invalid parameters\" parameter={}",
               valid.error().path);
    return;
  }
  Publish([&](ServerView& next) { next.accepted = accepted; });
  LI("subsystem=harness event=joined session={} character={} tick_rate_hz={}",
     static_cast<std::uint32_t>(accepted.session), accepted.character, accepted.tick_rate_hz);
}

void Inbox::OnJoinRefused(JoinRefusal reason) {
  Publish([&](ServerView& next) { next.refusal = reason; });
  LI("subsystem=harness event=join_refused reason=\"{}\"", DescribeJoinRefusal(reason));
}

void Inbox::OnLobby(Lobby lobby) {
  LI("subsystem=harness event=lobby version={} players={}", lobby.version, lobby.roster.size());
  Publish([&](ServerView& next) { next.lobby = std::move(lobby); });
}

// A Match start that leaves this client out is not one it can play: dropped,
// as a malformed message is.
void Inbox::OnMatchStart(MatchStart start) {
  const std::shared_ptr<const ServerView> current = view_.load();
  if (!current->accepted.has_value() || !IsInMatch(start, current->accepted->session)) {
    LW_LIMITED(drop_warnings_, "subsystem=harness event=dropped reason=\"match start without this client\"");
    return;
  }
  const std::size_t players = start.players.size();
  Publish([&](ServerView& next) {
    next.match_start = std::move(start);
    ++next.matches_started;
    next.in_match = true;
    next.match_end.reset();
    next.authoritative.reset();
    next.dead.clear();
  });
  LI("subsystem=harness event=match_started players={}", players);
}

void Inbox::OnMatchEnd(const MatchEnd& end) {
  Publish([&](ServerView& next) {
    next.in_match = false;
    next.authoritative.reset();
    next.match_end = end;
  });
  // The fight's last events, the Deaths that ended the match among them, are
  // still taken: they stay under this match, which a view takes until the next
  // starts.
  if (end.winner.has_value()) {
    LI("subsystem=harness event=match_ended winner={}", std::to_underlying(*end.winner));
  } else {
    LI("subsystem=harness event=match_ended winner=draw");
  }
}

// Keeps shot for TakeShots if it is of the match in progress. One fired on
// the tick a match ended arrives after Match end, which is routine; one of a
// body not in the match is a server's mistake.
void Inbox::OnShot(const Shot& shot) {
  const std::shared_ptr<const ServerView> current = view_.load();
  if (!current->in_match) {
    LT("subsystem=harness event=dropped tick={} reason=\"shot outside a match\"", shot.tick);
    return;
  }
  if (!IsInMatch(*current->match_start, shot.shooter)) {
    LW_LIMITED(drop_warnings_, "subsystem=harness event=dropped tick={} reason=\"shot names a body not in the match\"",
               shot.tick);
    return;
  }
  shots_.Add(current->matches_started, shot);
}

// Keeps hit for TakeHitConfirmations if it is of the match in progress, as OnShot does a Shot.
void Inbox::OnHitConfirmation(const HitConfirmation& hit) {
  const std::shared_ptr<const ServerView> current = view_.load();
  if (!current->in_match) {
    LT("subsystem=harness event=dropped reason=\"hit confirmation outside a match\"");
    return;
  }
  if (!IsInMatch(*current->match_start, hit.target)) {
    LW_LIMITED(drop_warnings_,
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
  const std::shared_ptr<const ServerView> current = view_.load();
  if (!current->in_match) {
    LT("subsystem=harness event=dropped reason=\"death outside a match\"");
    return;
  }
  if (!IsInMatch(*current->match_start, death.victim) || !IsInMatch(*current->match_start, death.killer)) {
    LW_LIMITED(drop_warnings_, "subsystem=harness event=dropped reason=\"death names a body not in the match\"");
    return;
  }
  Publish([&](ServerView& next) { next.dead.push_back(death.victim); });
  deaths_.Add(current->matches_started, death);
  LI("subsystem=harness event=death victim={} killer={} part={}", std::to_underlying(death.victim),
     std::to_underlying(death.killer), BodyPartName(death.part));
}

// Keeps state if it is newer than the one held (unreliable delivery can
// reorder) and belongs to the match in progress: unreliable, it can arrive
// before Match start or after Match end.
void Inbox::OnAuthoritativeState(AuthoritativeState state) {
  const std::shared_ptr<const ServerView> current = view_.load();
  if (!current->in_match) {
    LT("subsystem=harness event=dropped tick={} reason=\"state outside a match\"", state.tick);
    return;
  }
  const auto in_match = [&](const EntityBody& body) { return IsInMatch(*current->match_start, body.entity); };
  if (!std::ranges::all_of(state.bodies, in_match)) {
    LW_LIMITED(drop_warnings_, "subsystem=harness event=dropped tick={} reason=\"state names a body not in the match\"",
               state.tick);
    return;
  }
  if (current->authoritative.has_value() && state.tick <= current->authoritative->tick) {
    return;
  }
  Publish([&](ServerView& next) { next.authoritative = std::move(state); });
}

}  // namespace augusta::harness
