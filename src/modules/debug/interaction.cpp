#include <cstddef>
#include <span>
#include <variant>

#include "augusta/debug_interaction.h"
#include "augusta/window_events.h"

namespace augusta::debug {

namespace {

// Whether event is key's press, or its release.
bool IsKey(const window::Event& event, window::Key key, window::KeyState state) {
  const auto* key_event = std::get_if<window::KeyEvent>(&event);
  return key_event != nullptr && key_event->key == key && key_event->state == state;
}

}  // namespace

DebugInteraction::DebugInteraction(InteractionKeys keys) : keys_(keys) {}

Routing DebugInteraction::Route(std::span<const window::Event> events) {
  Routing routing;
  for (const window::Event& event : events) {
    if (IsKey(event, keys_.toggle, window::KeyState::kPressed)) {
      if (in_) {
        Leave(routing);
      } else {
        in_ = true;
        routing.entered = true;
      }
    } else if (in_ && IsKey(event, keys_.leave, window::KeyState::kPressed)) {
      Leave(routing);
      routing.left_by_leave_key = true;
      leave_release_owed_ = true;
    } else if (IsKey(event, keys_.toggle, window::KeyState::kReleased)) {
      // The toggle key is Debug interaction's alone.
    } else if (leave_release_owed_ && IsKey(event, keys_.leave, window::KeyState::kReleased)) {
      leave_release_owed_ = false;
    } else {
      Route(event, routing);
    }
  }
  return routing;
}

bool DebugInteraction::In() const { return in_; }

bool DebugInteraction::CursorLocked(bool cursor_captured) const { return !in_ && cursor_captured; }

void DebugInteraction::Route(const window::Event& event, Routing& routing) {
  if (!in_) {
    routing.game.push_back(event);
    return;
  }
  if (const auto* key_event = std::get_if<window::KeyEvent>(&event)) {
    panels_held_[static_cast<std::size_t>(key_event->key)] = key_event->state == window::KeyState::kPressed;
  }
  routing.panels.push_back(event);
}

void DebugInteraction::Leave(Routing& routing) {
  in_ = false;
  for (std::size_t key = 0; key < panels_held_.size(); ++key) {
    if (panels_held_[key]) {
      routing.panels.emplace_back(
          window::KeyEvent{.key = static_cast<window::Key>(key), .state = window::KeyState::kReleased});
      panels_held_[key] = false;
    }
  }
  routing.panels.emplace_back(kCursorOffPanels);
}

}  // namespace augusta::debug
