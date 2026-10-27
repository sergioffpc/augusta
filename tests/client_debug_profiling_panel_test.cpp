#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/debug_panels.h"
#include "augusta/renderer.h"
#include "debug_profiling_panel.h"

// The Profiling Debug panel (ADR-0075): a window of N frames for Nsight
// Systems to capture, and Aftermath's status, built and updated without
// drawing anything.
namespace {

using augusta::client::CaptureChange;
using augusta::client::kProfilingPanelId;
using augusta::client::ProfilingPanel;
using augusta::debug::Button;
using augusta::debug::ButtonPress;
using augusta::debug::ControlEvent;
using augusta::debug::Panel;
using augusta::debug::Slider;
using augusta::debug::Text;
using augusta::renderer::GpuCrashDumpStatus;

const GpuCrashDumpStatus kWatched{.enabled = true, .attached = true, .directory = "C:/augusta/dumps"};

ControlEvent Press() { return {.panel = std::string(kProfilingPanelId), .control = "capture", .value = ButtonPress{}}; }

ControlEvent Drag(float frames) {
  return {.panel = std::string(kProfilingPanelId), .control = "frames", .value = frames};
}

template <typename Control>
Control Find(const Panel& panel) {
  for (const auto& element : panel.elements) {
    if (const auto* control = std::get_if<Control>(&element)) {
      return *control;
    }
  }
  ADD_FAILURE() << "the panel has no such control";
  return {};
}

std::vector<std::string> Texts(const Panel& panel) {
  std::vector<std::string> texts;
  for (const auto& element : panel.elements) {
    if (const auto* text = std::get_if<Text>(&element)) {
      texts.push_back(text->text);
    }
  }
  return texts;
}

bool HasText(const Panel& panel, std::string_view text) {
  const std::vector<std::string> texts = Texts(panel);
  return std::ranges::find(texts, text) != texts.end();
}

// Requirements: US-29
TEST(ProfilingPanelTest, IsTheProfilingPanel) {
  const ProfilingPanel profiling(kWatched);

  const Panel panel = profiling.BuildPanel();

  EXPECT_EQ(panel.id, kProfilingPanelId);
  EXPECT_EQ(panel.title, "Profiling");
}

// Requirements: US-29
TEST(ProfilingPanelTest, TheButtonOpensACaptureWindowOfTheDefaultThreeHundredFrames) {
  ProfilingPanel profiling(kWatched);

  EXPECT_EQ(Find<Slider>(profiling.BuildPanel()).value, 300.0F);
  EXPECT_EQ(Find<Button>(profiling.BuildPanel()).label, "Capture 300 frames");
  EXPECT_EQ(profiling.Update(Press()), CaptureChange::kOpens);
}

// Requirements: US-29
TEST(ProfilingPanelTest, NFrameTicksCloseTheWindow) {
  ProfilingPanel profiling(kWatched);
  ASSERT_EQ(profiling.Update(Press()), CaptureChange::kOpens);

  for (int frame = 1; frame < 300; ++frame) {
    ASSERT_EQ(profiling.Tick(), CaptureChange::kNone) << "frame " << frame;
  }
  EXPECT_EQ(profiling.Tick(), CaptureChange::kCloses);
  EXPECT_EQ(profiling.Tick(), CaptureChange::kNone);
}

// Requirements: US-29
TEST(ProfilingPanelTest, FrameTicksOutsideACaptureDoNothing) {
  ProfilingPanel profiling(kWatched);
  ASSERT_EQ(profiling.Update(Drag(1.0F)), CaptureChange::kNone);
  ASSERT_EQ(profiling.Update(Press()), CaptureChange::kOpens);
  ASSERT_EQ(profiling.Tick(), CaptureChange::kCloses);  // Control: in a capture, a tick closes it.

  EXPECT_EQ(profiling.Tick(), CaptureChange::kNone);
}

// Requirements: US-29
TEST(ProfilingPanelTest, TheButtonAndTheSliderAreGreyedOutDuringACaptureOnly) {
  ProfilingPanel profiling(kWatched);
  ASSERT_EQ(profiling.Update(Drag(2.0F)), CaptureChange::kNone);
  EXPECT_FALSE(Find<Button>(profiling.BuildPanel()).disabled);
  EXPECT_FALSE(Find<Slider>(profiling.BuildPanel()).disabled);

  ASSERT_EQ(profiling.Update(Press()), CaptureChange::kOpens);

  EXPECT_TRUE(Find<Button>(profiling.BuildPanel()).disabled);
  EXPECT_TRUE(Find<Slider>(profiling.BuildPanel()).disabled);
  ASSERT_EQ(profiling.Tick(), CaptureChange::kNone);
  ASSERT_EQ(profiling.Tick(), CaptureChange::kCloses);
  EXPECT_FALSE(Find<Button>(profiling.BuildPanel()).disabled);
  EXPECT_FALSE(Find<Slider>(profiling.BuildPanel()).disabled);
}

// Requirements: US-29
TEST(ProfilingPanelTest, ThePressOfAButtonDuringACaptureOpensNoSecondWindow) {
  ProfilingPanel profiling(kWatched);
  ASSERT_EQ(profiling.Update(Drag(2.0F)), CaptureChange::kNone);
  ASSERT_EQ(profiling.Update(Press()), CaptureChange::kOpens);
  ASSERT_EQ(profiling.Tick(), CaptureChange::kNone);

  EXPECT_EQ(profiling.Update(Press()), CaptureChange::kNone);
  EXPECT_EQ(profiling.Tick(), CaptureChange::kCloses);
}

// Requirements: US-29
TEST(ProfilingPanelTest, TheSliderChangesNWhenIdle) {
  ProfilingPanel profiling(kWatched);

  ASSERT_EQ(profiling.Update(Drag(12.6F)), CaptureChange::kNone);

  EXPECT_EQ(Find<Slider>(profiling.BuildPanel()).value, 13.0F);
  EXPECT_EQ(Find<Button>(profiling.BuildPanel()).label, "Capture 13 frames");
}

// Requirements: US-29
TEST(ProfilingPanelTest, TheSliderRangesFromOneToThreeThousandSixHundredFrames) {
  ProfilingPanel profiling(kWatched);

  const auto slider = Find<Slider>(profiling.BuildPanel());
  EXPECT_EQ(slider.min, 1.0F);
  EXPECT_EQ(slider.max, 3600.0F);
  ASSERT_EQ(profiling.Update(Drag(5000.0F)), CaptureChange::kNone);
  EXPECT_EQ(Find<Slider>(profiling.BuildPanel()).value, 3600.0F);
  ASSERT_EQ(profiling.Update(Drag(0.0F)), CaptureChange::kNone);
  EXPECT_EQ(Find<Slider>(profiling.BuildPanel()).value, 1.0F);
}

// Requirements: US-29
TEST(ProfilingPanelTest, TheSliderShowsNAsAWholeNumber) {
  const ProfilingPanel profiling(kWatched);

  EXPECT_TRUE(Find<Slider>(profiling.BuildPanel()).whole_numbers);
}

// Requirements: US-29
TEST(ProfilingPanelTest, TheSliderDoesNotChangeNDuringACapture) {
  ProfilingPanel profiling(kWatched);
  ASSERT_EQ(profiling.Update(Drag(2.0F)), CaptureChange::kNone);
  ASSERT_EQ(profiling.Update(Press()), CaptureChange::kOpens);

  ASSERT_EQ(profiling.Update(Drag(10.0F)), CaptureChange::kNone);

  EXPECT_EQ(Find<Slider>(profiling.BuildPanel()).value, 2.0F);
  ASSERT_EQ(profiling.Tick(), CaptureChange::kNone);
  EXPECT_EQ(profiling.Tick(), CaptureChange::kCloses);
}

// Requirements: US-29
TEST(ProfilingPanelTest, AnotherPanelsEventChangesNothing) {
  ProfilingPanel profiling(kWatched);

  EXPECT_EQ(profiling.Update({.panel = "network", .control = "frames", .value = 5.0F}), CaptureChange::kNone);
  EXPECT_EQ(profiling.Update({.panel = "network", .control = "capture", .value = ButtonPress{}}), CaptureChange::kNone);
  EXPECT_EQ(Find<Slider>(profiling.BuildPanel()).value, 300.0F);

  // Control: the same events, the Profiling panel's, set N and open a window.
  ASSERT_EQ(profiling.Update(Drag(5.0F)), CaptureChange::kNone);
  EXPECT_EQ(Find<Slider>(profiling.BuildPanel()).value, 5.0F);
  EXPECT_EQ(profiling.Update(Press()), CaptureChange::kOpens);
}

// Requirements: US-29
TEST(ProfilingPanelTest, TheProgressTextCountsTheFramesOfTheWindow) {
  ProfilingPanel profiling(kWatched);
  ASSERT_EQ(profiling.Update(Drag(3.0F)), CaptureChange::kNone);
  EXPECT_TRUE(HasText(profiling.BuildPanel(), "Not capturing"));

  ASSERT_EQ(profiling.Update(Press()), CaptureChange::kOpens);
  EXPECT_TRUE(HasText(profiling.BuildPanel(), "Capturing frame 1 of 3"));
  ASSERT_EQ(profiling.Tick(), CaptureChange::kNone);
  EXPECT_TRUE(HasText(profiling.BuildPanel(), "Capturing frame 2 of 3"));
  ASSERT_EQ(profiling.Tick(), CaptureChange::kNone);
  EXPECT_TRUE(HasText(profiling.BuildPanel(), "Capturing frame 3 of 3"));
  ASSERT_EQ(profiling.Tick(), CaptureChange::kCloses);
  EXPECT_TRUE(HasText(profiling.BuildPanel(), "Not capturing"));
}

// Requirements: US-29
TEST(ProfilingPanelTest, TheAftermathLinesSayItIsEnabledAttachedAndWhereADumpLands) {
  const ProfilingPanel profiling(kWatched);

  const Panel panel = profiling.BuildPanel();

  EXPECT_TRUE(HasText(panel, "Aftermath: enabled"));
  EXPECT_TRUE(HasText(panel, "Attached to the device: yes"));
  EXPECT_TRUE(HasText(panel, "Dumps land in: " + std::filesystem::path("C:/augusta/dumps").string()));
}

// Requirements: US-29
TEST(ProfilingPanelTest, TheAftermathLinesSayWhenItIsNeitherEnabledNorAttached) {
  const ProfilingPanel profiling(GpuCrashDumpStatus{.enabled = false, .attached = false, .directory = "dumps"});

  const Panel panel = profiling.BuildPanel();

  EXPECT_TRUE(HasText(panel, "Aftermath: not enabled"));
  EXPECT_TRUE(HasText(panel, "Attached to the device: no"));
  EXPECT_TRUE(HasText(panel, "Dumps land in: dumps"));
}

}  // namespace
