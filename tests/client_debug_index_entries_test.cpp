#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/debug_index.h"
#include "augusta/physics_overlays.h"
#include "debug_index_entries.h"
#include "profiling_panel.h"

// Which debug tools the client Runtime's Debug index lists, by the client's
// mode (ADR-0075): private to the Runtime, reached through its directory.
namespace augusta::client {
namespace {

std::vector<std::string> IdsOf(const std::vector<debug::IndexEntry>& entries) {
  std::vector<std::string> ids;
  ids.reserve(entries.size());
  for (const debug::IndexEntry& entry : entries) {
    ids.push_back(entry.id);
  }
  return ids;
}

const DebugIndexStart kAllOn{.hud = true, .profiling_panel = true, .physics_panel = true};

// A player who dies spectates in the same mode, so its Debug index stays.
TEST(DebugIndexEntriesTest, APlayerAndACapturedPlayerListThePhysicsPanel) {
  for (const ClientMode mode : {ClientMode::kPlayer, ClientMode::kCapturedPlayer}) {
    EXPECT_EQ(
        IdsOf(DebugIndexEntries(kAllOn, mode)),
        (std::vector<std::string>{"hud", std::string(kProfilingPanelId), std::string(presentation::kPhysicsPanelId)}));
  }
}

TEST(DebugIndexEntriesTest, AReplayViewerListsNoPhysicsPanel) {
  EXPECT_EQ(IdsOf(DebugIndexEntries(kAllOn, ClientMode::kReplayViewer)),
            (std::vector<std::string>{"hud", std::string(kProfilingPanelId)}));
}

TEST(DebugIndexEntriesTest, EachEntryStartsAsConfigured) {
  const std::vector<debug::IndexEntry> entries =
      DebugIndexEntries({.hud = false, .profiling_panel = false, .physics_panel = true}, ClientMode::kPlayer);

  ASSERT_EQ(entries.size(), 3U);
  EXPECT_FALSE(entries[0].on);
  EXPECT_FALSE(entries[1].on);
  EXPECT_TRUE(entries[2].on);
  EXPECT_EQ(entries[2].name, "Physics");
}

}  // namespace
}  // namespace augusta::client
