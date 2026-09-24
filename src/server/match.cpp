#include "match.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace augusta::server {

Match::Match(MatchConfig config, std::vector<math::Vec3> spawn_points)
    : engine_version_(std::move(config.engine_version)),
      characters_(std::move(config.characters)),
      player_count_(config.player_count),
      pause_ticks_(config.pause_ticks),
      spawn_points_(std::move(spawn_points)),
      ticks_since_end_(config.pause_ticks) {
  if (spawn_points_.empty()) {
    spawn_points_.emplace_back();
  }
}

std::expected<Admission, protocol::JoinRefusal> Match::Join(networking::PeerId peer, std::string_view engine_version,
                                                            std::string_view character) {
  if (const auto existing = members_.find(peer); existing != members_.end()) {
    return Admission{.session = existing->second.session, .character = existing->second.character};
  }
  // A client that can never play here should hear that before it hears "wait":
  // the version, then the character, then whether it could join later.
  if (engine_version != engine_version_) {
    return std::unexpected(protocol::JoinRefusal::kVersionMismatch);
  }
  const auto found = std::ranges::find(characters_, character);
  if (found == characters_.end()) {
    return std::unexpected(protocol::JoinRefusal::kUnknownCharacter);
  }
  if (in_match_) {
    return std::unexpected(protocol::JoinRefusal::kMatchInProgress);
  }
  if (members_.size() >= player_count_) {
    return std::unexpected(protocol::JoinRefusal::kLobbyFull);
  }
  // The scenario composes at most assets::kMaxCharacters, so an index fits in a byte.
  const auto index = static_cast<std::uint8_t>(std::distance(characters_.begin(), found) + 1);
  // A newcomer bumps the version, so no one is Ready until they have loaded its character.
  const Member member{.session = static_cast<protocol::SessionId>(next_session_++), .character = index};
  members_.emplace(peer, member);
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
                                        .character = member.character,
                                        .spawn = spawn_points_[next_spawn_++ % spawn_points_.size()]});
  }
  return start;
}

std::vector<protocol::SessionId> Match::End() {
  std::vector<protocol::SessionId> ended = Playing();
  if (!in_match_) {
    return ended;
  }
  in_match_ = false;
  ++roster_version_;
  ticks_since_end_ = 0;
  return ended;
}

bool Match::InMatch() const { return in_match_; }

bool Match::IsPlaying(protocol::SessionId session) const {
  return in_match_ && std::ranges::any_of(members_, [&](const auto& entry) { return entry.second.session == session; });
}

std::vector<protocol::SessionId> Match::Playing() const {
  std::vector<protocol::SessionId> playing;
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

std::optional<protocol::SessionId> Match::SessionOf(networking::PeerId peer) const {
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
