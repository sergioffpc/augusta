#include "lobby_readiness.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "augusta/harness.h"

namespace augusta::client {

std::optional<ReadyPlan> LobbyReadiness::Plan(const harness::ServerView& view) const {
  const std::optional<harness::Lobby>& lobby = view.lobby;
  if (view.GetPhase() != harness::Phase::kLobby || !lobby.has_value() || lobby->version == ready_version_) {
    return std::nullopt;
  }
  ReadyPlan plan{.version = lobby->version, .characters_to_load = {}};
  for (const harness::RosterEntry& entry : lobby->roster) {
    if (entry.session != view.accepted->session && !loaded_characters_.contains(entry.character) &&
        std::ranges::find(plan.characters_to_load, entry.character) == plan.characters_to_load.end()) {
      plan.characters_to_load.push_back(entry.character);
    }
  }
  return plan;
}

void LobbyReadiness::MarkLoaded(std::string character) { loaded_characters_.insert(std::move(character)); }

void LobbyReadiness::MarkReady(std::uint32_t version) { ready_version_ = version; }

}  // namespace augusta::client
