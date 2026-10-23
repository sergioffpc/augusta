#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/client_config.h"
#include "augusta/debug_index.h"
#include "augusta/physics_overlays.h"
#include "debug_index_entries.h"
#include "network_panel.h"
#include "profiling_panel.h"

// Which debug tools the client Runtime's Debug index lists, and which Physics
// overlays start on, by the client's mode (ADR-0075): private to the Runtime,
// reached through its directory.
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

const DebugIndexStart kAllOn{.hud = true, .profiling_panel = true, .network_panel = true, .physics_panel = true};

// A player who dies spectates in the same mode, so its Debug index stays.
TEST(DebugIndexEntriesTest, APlayerAndACapturedPlayerListThePhysicsPanel) {
  for (const ClientMode mode : {ClientMode::kPlayer, ClientMode::kCapturedPlayer}) {
    EXPECT_EQ(IdsOf(DebugIndexEntries(kAllOn, mode)),
              (std::vector<std::string>{"hud", std::string(kProfilingPanelId), std::string(kNetworkPanelId),
                                        std::string(presentation::kPhysicsPanelId)}));
  }
}

TEST(DebugIndexEntriesTest, AReplayViewerListsNoPhysicsPanel) {
  EXPECT_EQ(IdsOf(DebugIndexEntries(kAllOn, ClientMode::kReplayViewer)),
            (std::vector<std::string>{"hud", std::string(kProfilingPanelId), std::string(kNetworkPanelId)}));
}

TEST(DebugIndexEntriesTest, EachEntryStartsAsConfigured) {
  const std::vector<debug::IndexEntry> entries = DebugIndexEntries(
      {.hud = false, .profiling_panel = false, .network_panel = true, .physics_panel = true}, ClientMode::kPlayer);

  ASSERT_EQ(entries.size(), 4U);
  EXPECT_FALSE(entries[0].on);
  EXPECT_FALSE(entries[1].on);
  EXPECT_TRUE(entries[2].on);
  EXPECT_EQ(entries[2].name, "Network");
  EXPECT_TRUE(entries[3].on);
  EXPECT_EQ(entries[3].name, "Physics");
}

TEST(ModeOfTest, AReplayIsWatchedByAReplayViewerAReenactmentPlayedByACapturedPlayer) {
  EXPECT_EQ(ModeOf(/*watches_replay=*/false, /*reenacts=*/false), ClientMode::kPlayer);
  EXPECT_EQ(ModeOf(/*watches_replay=*/false, /*reenacts=*/true), ClientMode::kCapturedPlayer);
  EXPECT_EQ(ModeOf(/*watches_replay=*/true, /*reenacts=*/false), ClientMode::kReplayViewer);
}

const config::ClientPhysicsDebugConfig kEveryOverlayOn{
    .panel = true, .collision = true, .server_bodies = true, .hitboxes = true, .shot_judgements = true};

TEST(PhysicsOverlaysStartOfTest, APlayerAndACapturedPlayerStartTheOverlaysAsConfigured) {
  const config::ClientPhysicsDebugConfig some{.collision = true, .hitboxes = true};
  for (const ClientMode mode : {ClientMode::kPlayer, ClientMode::kCapturedPlayer}) {
    EXPECT_EQ(PhysicsOverlaysStartOf(kEveryOverlayOn, mode),
              (presentation::PhysicsOverlaysStart{
                  .collision = true, .server_bodies = true, .hitboxes = true, .shot_judgements = true}));
    EXPECT_EQ(PhysicsOverlaysStartOf(some, mode),
              (presentation::PhysicsOverlaysStart{.collision = true, .hitboxes = true}));
  }
}

// Its Debug index lists no Physics panel to switch them.
TEST(PhysicsOverlaysStartOfTest, AReplayViewerStartsNone) {
  // The control: a player starts them all.
  ASSERT_NE(PhysicsOverlaysStartOf(kEveryOverlayOn, ClientMode::kPlayer), presentation::PhysicsOverlaysStart{});

  EXPECT_EQ(PhysicsOverlaysStartOf(kEveryOverlayOn, ClientMode::kReplayViewer), presentation::PhysicsOverlaysStart{});
}

}  // namespace
}  // namespace augusta::client
