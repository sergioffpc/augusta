#include "match.h"

#include <utility>

namespace augusta::server {

Match::Match(std::string engine_version, std::size_t capacity)
    : engine_version_(std::move(engine_version)), capacity_(capacity) {}

std::expected<protocol::SessionId, protocol::JoinRefusal> Match::Join(networking::PeerId peer,
                                                                      std::string_view engine_version) {
  if (const auto existing = sessions_.find(peer); existing != sessions_.end()) {
    return existing->second;
  }
  // The version comes first: a client that can never play here should hear
  // that, not "full".
  if (engine_version != engine_version_) {
    return std::unexpected(protocol::JoinRefusal::kVersionMismatch);
  }
  if (sessions_.size() >= capacity_) {
    return std::unexpected(protocol::JoinRefusal::kMatchFull);
  }
  const auto session = static_cast<protocol::SessionId>(next_session_++);
  sessions_.emplace(peer, session);
  return session;
}

void Match::Leave(networking::PeerId peer) { sessions_.erase(peer); }

std::optional<protocol::SessionId> Match::SessionOf(networking::PeerId peer) const {
  const auto found = sessions_.find(peer);
  return found == sessions_.end() ? std::nullopt : std::optional(found->second);
}

std::size_t Match::PlayerCount() const { return sessions_.size(); }

}  // namespace augusta::server
