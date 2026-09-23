#include "match.h"

#include <algorithm>
#include <utility>

namespace augusta::server {

Match::Match(MatchConfig config, std::vector<math::Vec3> spawn_points)
    : engine_version_(std::move(config.engine_version)),
      characters_(std::move(config.characters)),
      capacity_(config.capacity),
      spawn_points_(std::move(spawn_points)) {
  if (spawn_points_.empty()) {
    spawn_points_.emplace_back();
  }
}

std::expected<Admission, protocol::JoinRefusal> Match::Join(networking::PeerId peer, std::string_view engine_version,
                                                            std::string_view character) {
  if (const auto existing = members_.find(peer); existing != members_.end()) {
    const Member& member = existing->second;
    return Admission{.session = member.session, .spawn = member.spawn, .roster = RosterExcluding(member.session)};
  }
  // The version comes first, then the character: a client that can never play
  // here should hear that, not "full".
  if (engine_version != engine_version_) {
    return std::unexpected(protocol::JoinRefusal::kVersionMismatch);
  }
  if (std::ranges::find(characters_, character) == characters_.end()) {
    return std::unexpected(protocol::JoinRefusal::kUnknownCharacter);
  }
  if (members_.size() >= capacity_) {
    return std::unexpected(protocol::JoinRefusal::kMatchFull);
  }
  const auto session = static_cast<protocol::SessionId>(next_session_++);
  const math::Vec3 spawn = spawn_points_[next_spawn_++ % spawn_points_.size()];
  Admission admission{.session = session, .spawn = spawn, .roster = RosterExcluding(session)};
  Member member{.session = session, .spawn = spawn, .body = {}};
  member.body.position = spawn;
  members_.emplace(peer, member);
  return admission;
}

void Match::UpdateBody(protocol::SessionId session, const physics::BodyState& body) {
  for (auto& [peer, member] : members_) {
    if (member.session == session) {
      member.body = body;
      return;
    }
  }
}

void Match::Leave(networking::PeerId peer) { members_.erase(peer); }

std::optional<protocol::SessionId> Match::SessionOf(networking::PeerId peer) const {
  const auto found = members_.find(peer);
  return found == members_.end() ? std::nullopt : std::optional(found->second.session);
}

std::size_t Match::PlayerCount() const { return members_.size(); }

std::vector<protocol::PlayerState> Match::RosterExcluding(protocol::SessionId session) const {
  std::vector<protocol::PlayerState> roster;
  for (const auto& [peer, member] : members_) {
    if (member.session != session) {
      roster.push_back(protocol::PlayerState{.session = member.session, .body = member.body});
    }
  }
  std::ranges::sort(roster, {}, &protocol::PlayerState::session);
  return roster;
}

}  // namespace augusta::server
