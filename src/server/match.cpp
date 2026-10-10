#include "match.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/networking.h"

namespace augusta::server {

Match::Match(MatchConfig config)
    : terms_{.engine_version = std::move(config.engine_version), .client_pack = config.client_pack},
      characters_(std::move(config.characters)),
      player_count_(config.player_count),
      pause_ticks_(config.pause_ticks),
      reenactments_(config.reenactments),
      ticks_since_end_(config.pause_ticks) {}

std::string_view DescribeJoinRefusal(JoinRefusal reason) {
  switch (reason) {
    case JoinRefusal::kVersionMismatch:
      return "client version does not match the server";
    case JoinRefusal::kLobbyFull:
      return "the lobby is full, or the replay server runs all the Replays it may";
    case JoinRefusal::kUnknownCharacter:
      return "the server's scenario has no such character";
    case JoinRefusal::kMatchInProgress:
      return "a match is in progress: try again once it ends";
    case JoinRefusal::kPackMismatch:
      return "client pack does not match the server's";
    case JoinRefusal::kReplayServer:
      return "the server is a replay server: it only replays Match captures";
    case JoinRefusal::kUnknownCapture:
      return "the replay server replays no such capture";
    case JoinRefusal::kNotAReplayServer:
      return "the server is a live server, not a replay server";
    case JoinRefusal::kReenactmentsNotAccepted:
      return "the server does not take reenactments";
  }
  return "unknown refusal";
}

std::optional<JoinRefusal> RefusalOfClient(std::string_view engine_version, const assets::PackHash& client_pack,
                                           const ClientTerms& terms) {
  if (engine_version != terms.engine_version) {
    return JoinRefusal::kVersionMismatch;
  }
  if (client_pack != terms.client_pack) {
    return JoinRefusal::kPackMismatch;
  }
  return std::nullopt;
}

std::expected<Admission, JoinRefusal> Match::Join(networking::PeerId peer, const JoinRequest& request) {
  if (const auto existing = members_.find(peer); existing != members_.end()) {
    return Admission{.session = existing->second.session, .character = existing->second.character};
  }
  // A client that can never play here should hear that before it hears "wait":
  // whether it may name its spawn at all, then the version and the pack its
  // characters come from, as any client's, then the character, then whether
  // it could join later.
  if (request.spawn.has_value() && !reenactments_) {
    return std::unexpected(JoinRefusal::kReenactmentsNotAccepted);
  }
  if (const std::optional<JoinRefusal> refusal = RefusalOfClient(request.engine_version, request.client_pack, terms_)) {
    return std::unexpected(*refusal);
  }
  const auto found = std::ranges::find(characters_, request.character);
  if (found == characters_.end()) {
    return std::unexpected(JoinRefusal::kUnknownCharacter);
  }
  if (in_match_) {
    return std::unexpected(JoinRefusal::kMatchInProgress);
  }
  if (members_.size() >= player_count_) {
    return std::unexpected(JoinRefusal::kLobbyFull);
  }
  // A newcomer bumps the version, so no one is Ready until they have loaded its character.
  const Member& member = members_
                             .emplace(peer, Member{.session = static_cast<SessionId>(next_session_++),
                                                   .character = *found,
                                                   .spawn = request.spawn})
                             .first->second;
  ++roster_version_;
  return Admission{.session = member.session, .character = member.character};
}

Departure Match::Leave(networking::PeerId peer) {
  if (members_.erase(peer) == 0) {
    return Departure::kNone;
  }
  if (in_match_) {
    if (members_.empty()) {
      End();
      return Departure::kEndedMatch;
    }
    return Departure::kFromMatch;
  }
  // Whoever had loaded everyone before the departure still has everyone loaded after it.
  const std::uint32_t previous = roster_version_++;
  for (auto& [other, member] : members_) {
    if (member.ready_version == previous) {
      member.ready_version = roster_version_;
    }
  }
  return Departure::kFromLobby;
}

bool Match::Ready(networking::PeerId peer, std::uint32_t version) {
  const auto found = members_.find(peer);
  if (in_match_ || found == members_.end() || version != roster_version_) {
    return false;
  }
  found->second.ready_version = version;
  return true;
}

bool Match::IsReady(networking::PeerId peer) const {
  const auto found = members_.find(peer);
  return !in_match_ && found != members_.end() && found->second.ready_version == roster_version_;
}

void Match::Tick() {
  if (!in_match_ && ticks_since_end_ < pause_ticks_) {
    ++ticks_since_end_;
  }
}

std::optional<MatchStart> Match::TryStart() {
  if (in_match_ || members_.size() != player_count_ || ticks_since_end_ < pause_ticks_) {
    return std::nullopt;
  }
  if (!std::ranges::all_of(members_,
                           [&](const auto& entry) { return entry.second.ready_version == roster_version_; })) {
    return std::nullopt;
  }
  in_match_ = true;
  MatchStart start;
  for (const Member& member : MembersBySession()) {
    start.players.push_back(MatchPlayer{.session = member.session,
                                        .entity = static_cast<EntityId>(next_entity_++),
                                        .character = member.character,
                                        .spawn = member.spawn});
  }
  return start;
}

std::optional<MatchEnd> Match::End(std::optional<SessionId> winner) {
  if (!in_match_) {
    return std::nullopt;
  }
  MatchEnd ended{.players = Playing(), .winner = std::nullopt};
  if (winner.has_value() && IsPlaying(*winner)) {
    ended.winner = winner;
  }
  in_match_ = false;
  ++roster_version_;
  ticks_since_end_ = 0;
  return ended;
}

bool Match::InMatch() const { return in_match_; }

bool Match::IsPlaying(SessionId session) const {
  return in_match_ && std::ranges::any_of(members_, [&](const auto& entry) { return entry.second.session == session; });
}

std::vector<SessionId> Match::Playing() const {
  std::vector<SessionId> playing;
  if (in_match_) {
    for (const Member& member : MembersBySession()) {
      playing.push_back(member.session);
    }
  }
  return playing;
}

Roster Match::GetRoster() const {
  Roster roster{.version = roster_version_, .players = {}};
  if (!in_match_) {
    for (const Member& member : MembersBySession()) {
      roster.players.push_back(RosterEntry{.session = member.session, .character = member.character});
    }
  }
  return roster;
}

std::optional<SessionId> Match::SessionOf(networking::PeerId peer) const {
  const auto found = members_.find(peer);
  return found == members_.end() ? std::nullopt : std::optional(found->second.session);
}

std::size_t Match::PlayerCount() const { return members_.size(); }

std::vector<Match::Member> Match::MembersBySession() const {
  std::vector<Member> members;
  members.reserve(members_.size());
  for (const auto& [peer, member] : members_) {
    members.push_back(member);
  }
  std::ranges::sort(members, {}, &Member::session);
  return members;
}

}  // namespace augusta::server
