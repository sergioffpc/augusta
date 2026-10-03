#ifndef AUGUSTA_CLIENT_LOBBY_READINESS_H_
#define AUGUSTA_CLIENT_LOBBY_READINESS_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "augusta/harness.h"

// Decides what the client loads in the Lobby before it reports Ready, and for
// which Roster (ADR-0043). It decides only: ClientRuntime loads the characters
// and reports Ready, then tells it so.
namespace augusta::client {

/// What to do before reporting Ready for one Roster.
struct ReadyPlan {
  /// The Roster's version, which the Ready names.
  std::uint32_t version = 0;
  /// Every other player's character not loaded yet, each once, in Roster order.
  std::vector<std::string> characters_to_load;
};

/// Which characters the client has loaded (for the life of the process) and the
/// newest Roster it reported Ready for. Main/Render thread only.
class LobbyReadiness {
 public:
  /// What to do for view's Roster, or nullopt when there is nothing to do: not
  /// in the Lobby, or already Ready for that Roster.
  [[nodiscard]] std::optional<ReadyPlan> Plan(const harness::ServerView& view) const;

  /// Records that character is loaded, so no later plan loads it again.
  void MarkLoaded(std::string character);

  /// Records that Ready was reported for the Roster numbered version.
  void MarkReady(std::uint32_t version);

 private:
  std::set<std::string, std::less<>> loaded_characters_;
  std::optional<std::uint32_t> ready_version_;
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_LOBBY_READINESS_H_
