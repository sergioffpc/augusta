#include "augusta/debug_interaction.h"

#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/window_events.h"

namespace augusta::debug {
namespace {

using window::Event;
using window::Key;
using window::KeyEvent;
using window::KeyState;
using window::MouseMoveEvent;

// The keys as the client gives them: F1 toggles, Escape leaves.
constexpr InteractionKeys kKeys{.toggle = Key::kF1, .leave = Key::kEscape};

Event Press(Key key) { return KeyEvent{.key = key, .state = KeyState::kPressed}; }

Event Release(Key key) { return KeyEvent{.key = key, .state = KeyState::kReleased}; }

// The keys of a batch's key events, in order, which is what tells one apart
// from another here.
std::vector<Key> KeysOf(const std::vector<Event>& events) {
  std::vector<Key> keys;
  for (const Event& event : events) {
    if (const auto* key = std::get_if<KeyEvent>(&event)) {
      keys.push_back(key->key);
    }
  }
  return keys;
}

// A DebugInteraction already in Debug interaction.
DebugInteraction Entered() {
  DebugInteraction interaction(kKeys);
  const std::vector<Event> f1{Press(Key::kF1)};
  (void)interaction.Route(f1);
  return interaction;
}

TEST(DebugInteractionTest, StartsOutWithEveryEventGoingToTheGame) {
  DebugInteraction interaction(kKeys);
  const std::vector<Event> events{Press(Key::kW), MouseMoveEvent{.x = 3.0F, .y = 4.0F}, Release(Key::kW)};

  const Routing routing = interaction.Route(events);

  EXPECT_FALSE(interaction.In());
  EXPECT_EQ(routing.game.size(), 3U);
  EXPECT_EQ(KeysOf(routing.game), (std::vector<Key>{Key::kW, Key::kW}));
  EXPECT_TRUE(routing.panels.empty());
  EXPECT_FALSE(routing.entered);
}

TEST(DebugInteractionTest, F1FromOutEnters) {
  DebugInteraction interaction(kKeys);
  const std::vector<Event> events{Press(Key::kF1)};

  const Routing routing = interaction.Route(events);

  EXPECT_TRUE(interaction.In());
  EXPECT_TRUE(routing.entered);
}

TEST(DebugInteractionTest, F1FromInLeavesNotByEscape) {
  DebugInteraction interaction = Entered();
  const std::vector<Event> events{Press(Key::kF1)};

  const Routing routing = interaction.Route(events);

  EXPECT_FALSE(interaction.In());
  EXPECT_FALSE(routing.entered);
  EXPECT_FALSE(routing.left_by_escape);
}

TEST(DebugInteractionTest, EventsBeforeF1GoToTheGameAndAfterItToThePanels) {
  DebugInteraction interaction(kKeys);
  const std::vector<Event> events{Press(Key::kW), Press(Key::kF1), Press(Key::kMouseLeft)};

  const Routing routing = interaction.Route(events);

  EXPECT_EQ(KeysOf(routing.game), (std::vector<Key>{Key::kW}));
  EXPECT_EQ(KeysOf(routing.panels), (std::vector<Key>{Key::kMouseLeft}));
}

TEST(DebugInteractionTest, NeitherF1NorEscapeReachesTheGameOrThePanels) {
  DebugInteraction interaction(kKeys);
  // In and out by F1, in again, then out by Escape, with every release.
  const std::vector<Event> events{Press(Key::kF1), Release(Key::kF1), Press(Key::kF1),     Release(Key::kF1),
                                  Press(Key::kF1), Release(Key::kF1), Press(Key::kEscape), Release(Key::kEscape)};

  const Routing routing = interaction.Route(events);

  EXPECT_TRUE(KeysOf(routing.game).empty());
  EXPECT_TRUE(KeysOf(routing.panels).empty());
}

TEST(DebugInteractionTest, EscapeLeavesAndSaysSo) {
  DebugInteraction interaction = Entered();
  const std::vector<Event> events{Press(Key::kEscape)};

  const Routing routing = interaction.Route(events);

  EXPECT_FALSE(interaction.In());
  EXPECT_TRUE(routing.left_by_escape);
}

TEST(DebugInteractionTest, EscapeOutOfDebugInteractionGoesToTheGame) {
  DebugInteraction interaction(kKeys);
  const std::vector<Event> events{Press(Key::kEscape), Release(Key::kEscape)};

  const Routing routing = interaction.Route(events);

  EXPECT_FALSE(interaction.In());
  EXPECT_FALSE(routing.left_by_escape);
  EXPECT_EQ(KeysOf(routing.game), (std::vector<Key>{Key::kEscape, Key::kEscape}));
}

TEST(DebugInteractionTest, LeavingLetsGoOfWhatThePanelsHoldAndTakesTheCursorOffThem) {
  DebugInteraction interaction = Entered();
  const std::vector<Event> held{Press(Key::kMouseLeft), MouseMoveEvent{.x = 5.0F, .y = 6.0F}, Press(Key::kA),
                                Release(Key::kA)};
  (void)interaction.Route(held);
  const std::vector<Event> f1{Press(Key::kF1)};

  const Routing routing = interaction.Route(f1);

  // The button still down is let go, the key already up is not, and the
  // cursor goes where no panel is, so no press or hover outlives Debug interaction.
  EXPECT_EQ(KeysOf(routing.panels), (std::vector<Key>{Key::kMouseLeft}));
  ASSERT_FALSE(routing.panels.empty());
  const auto* move = std::get_if<MouseMoveEvent>(&routing.panels.back());
  ASSERT_NE(move, nullptr);
  EXPECT_EQ(move->x, kCursorOffPanels.x);
  EXPECT_EQ(move->y, kCursorOffPanels.y);
}

TEST(DebugInteractionTest, TheCursorIsLockedOnlyWhenCapturedAndOut) {
  const DebugInteraction out(kKeys);
  const DebugInteraction in = Entered();

  EXPECT_TRUE(out.CursorLocked(true));
  EXPECT_FALSE(out.CursorLocked(false));
  EXPECT_FALSE(in.CursorLocked(true));
  EXPECT_FALSE(in.CursorLocked(false));
}

}  // namespace
}  // namespace augusta::debug
