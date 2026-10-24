#include "debug_index_entries.h"

#include <string>
#include <vector>

#include "augusta/client_config.h"
#include "augusta/debug_index.h"
#include "augusta/physics_overlays.h"
#include "debug_network_panel.h"
#include "debug_profiling_panel.h"

namespace augusta::client {

ClientMode ModeOf(bool watches_replay, bool reenacts) {
  if (watches_replay) {
    return ClientMode::kReplayViewer;
  }
  return reenacts ? ClientMode::kCapturedPlayer : ClientMode::kPlayer;
}

std::vector<debug::IndexEntry> DebugIndexEntries(const DebugIndexStart& start, ClientMode mode) {
  std::vector<debug::IndexEntry> entries{
      {.id = std::string(kDebugHudEntry), .name = "Debug HUD", .on = start.hud},
      {.id = std::string(kProfilingPanelId), .name = "Profiling", .on = start.profiling_panel},
      {.id = std::string(kNetworkPanelId), .name = "Network", .on = start.network_panel}};
  if (mode != ClientMode::kReplayViewer) {
    entries.push_back({.id = std::string(presentation::kPhysicsPanelId), .name = "Physics", .on = start.physics_panel});
  }
  return entries;
}

presentation::PhysicsOverlaysStart PhysicsOverlaysStartOf(const config::ClientPhysicsDebugConfig& configured,
                                                          ClientMode mode) {
  if (mode == ClientMode::kReplayViewer) {
    return {};
  }
  return {.collision = configured.collision,
          .server_bodies = configured.server_bodies,
          .hitboxes = configured.hitboxes,
          .shot_judgements = configured.shot_judgements};
}

}  // namespace augusta::client
