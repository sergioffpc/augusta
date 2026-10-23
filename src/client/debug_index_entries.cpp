#include "debug_index_entries.h"

#include <string>
#include <vector>

#include "augusta/debug_index.h"
#include "augusta/physics_overlays.h"
#include "profiling_panel.h"

namespace augusta::client {

std::vector<debug::IndexEntry> DebugIndexEntries(const DebugIndexStart& start, ClientMode mode) {
  std::vector<debug::IndexEntry> entries{
      {.id = std::string(kDebugHudEntry), .name = "Debug HUD", .on = start.hud},
      {.id = std::string(kProfilingPanelId), .name = "Profiling", .on = start.profiling_panel}};
  if (mode != ClientMode::kReplayViewer) {
    entries.push_back({.id = std::string(presentation::kPhysicsPanelId), .name = "Physics", .on = start.physics_panel});
  }
  return entries;
}

}  // namespace augusta::client
