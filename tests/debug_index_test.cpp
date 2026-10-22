#include "augusta/debug_index.h"

#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/debug_panels.h"

namespace augusta::debug {
namespace {

DebugIndex HudAndProfiling() {
  return DebugIndex({
      {.id = "hud", .name = "Debug HUD", .on = true},
      {.id = "profiling", .name = "Profiling", .on = false},
  });
}

TEST(DebugIndexTest, ShowsEachEntryAsOneToggleInOrder) {
  const Panel panel = HudAndProfiling().BuildPanel();

  EXPECT_EQ(panel.id, kDebugIndexPanelId);
  ASSERT_EQ(panel.elements.size(), 2U);
  const auto* hud = std::get_if<Toggle>(&panel.elements[0]);
  const auto* profiling = std::get_if<Toggle>(&panel.elements[1]);
  ASSERT_NE(hud, nullptr);
  ASSERT_NE(profiling, nullptr);
  EXPECT_EQ(hud->id, "hud");
  EXPECT_EQ(hud->label, "Debug HUD");
  EXPECT_TRUE(hud->on);
  EXPECT_EQ(profiling->id, "profiling");
  EXPECT_EQ(profiling->label, "Profiling");
  EXPECT_FALSE(profiling->on);
}

TEST(DebugIndexTest, AToggleEventSwitchesItsEntryAndLeavesTheOthers) {
  DebugIndex index = HudAndProfiling();
  ASSERT_TRUE(index.IsOn("hud"));
  ASSERT_FALSE(index.IsOn("profiling"));

  index.Update(ControlEvent{.panel = std::string(kDebugIndexPanelId), .control = "hud", .value = false});

  EXPECT_FALSE(index.IsOn("hud"));
  EXPECT_FALSE(index.IsOn("profiling"));
  index.Update(ControlEvent{.panel = std::string(kDebugIndexPanelId), .control = "profiling", .value = true});
  EXPECT_FALSE(index.IsOn("hud"));
  EXPECT_TRUE(index.IsOn("profiling"));
}

TEST(DebugIndexTest, IgnoresAnotherPanelsEvent) {
  DebugIndex index = HudAndProfiling();

  // Another panel with a control of the same id.
  index.Update(ControlEvent{.panel = "profiling", .control = "hud", .value = false});

  EXPECT_TRUE(index.IsOn("hud"));
}

}  // namespace
}  // namespace augusta::debug
