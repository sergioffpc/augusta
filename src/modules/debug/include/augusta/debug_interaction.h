#ifndef AUGUSTA_DEBUG_INTERACTION_H_
#define AUGUSTA_DEBUG_INTERACTION_H_

#include <array>
#include <limits>
#include <span>
#include <vector>

#include "augusta/window_events.h"

/// \file
/// augusta::debug's Debug interaction: the state in which the Debug panels take
/// the mouse and keyboard and the game gets neither (GLOSSARY.md). One key
/// toggles it, another leaves it, and the client Runtime gives both from
/// input's reserved keys (F1 and Escape), so this module knows nothing of input.
/// It only decides where each of the window's events goes; the client Runtime
/// hands the game's share to input::Input and the panels' share to the Renderer
/// (Renderer::HandleDebugPanelInput), and does what entering and leaving ask of
/// Input. Pure, owning no window, and compiled only in a Development build
/// (ADR-0074, ADR-0075).
namespace augusta::debug {

/// The keys Debug interaction answers to. Neither the toggle key nor a leave
/// key's press that leaves, with its release, reaches the game or the panels.
struct InteractionKeys {
  /// Enters Debug interaction from out, and leaves it from in.
  window::Key toggle;
  /// Leaves Debug interaction, as it releases the cursor in the game; out of
  /// Debug interaction it is the game's, as any key.
  window::Key leave;
};

/// Where the cursor goes, for the panels, when Debug interaction ends: nowhere
/// on screen, so no panel stays hovered while the game plays on.
inline constexpr window::MouseMoveEvent kCursorOffPanels{.x = std::numeric_limits<float>::lowest(),
                                                         .y = std::numeric_limits<float>::lowest()};

/// Where one batch of the window's events went.
struct Routing {
  /// The events out of Debug interaction, oldest first: input::Input's.
  std::vector<window::Event> game;
  /// The events in Debug interaction, oldest first: the Debug panels'. Each
  /// time Debug interaction ends, they end with a release of every key and
  /// mouse button the panels were left holding and a move to kCursorOffPanels.
  std::vector<window::Event> panels;
  /// Debug interaction began during the batch: what the game holds is to be
  /// let go of (input::Input::LetGo), after the game's share.
  bool entered = false;
  /// Debug interaction ended by the leave key during the batch: the game's
  /// cursor is to be released, as that key does in the game
  /// (input::Input::ReleaseCursor), after the game's share.
  bool left_by_leave_key = false;
};

/// Out of Debug interaction at first. The client constructs one, and feeds it
/// every batch of the window's events, from the Main/Render thread.
class DebugInteraction {
 public:
  explicit DebugInteraction(InteractionKeys keys);

  /// Routes events, oldest first, as Window::Poll returns them, each by the
  /// state at its place in the batch.
  [[nodiscard]] Routing Route(std::span<const window::Event> events);

  /// Whether the client is in Debug interaction.
  [[nodiscard]] bool In() const;

  /// Whether the window's cursor is to be locked for mouselook, given whether
  /// the game captures it (input::Input::CursorCaptured): only out of Debug
  /// interaction, where the game has the mouse.
  [[nodiscard]] bool CursorLocked(bool cursor_captured) const;

 private:
  // Routes one event that is not Debug interaction's own into routing.
  void Route(const window::Event& event, Routing& routing);
  // Ends Debug interaction, letting go of what the panels hold.
  void Leave(Routing& routing);

  InteractionKeys keys_;
  bool in_ = false;
  // The leave key left Debug interaction, and its release, which is
  // Debug interaction's too, has not come yet.
  bool leave_release_owed_ = false;
  // The keys and mouse buttons the panels hold, pressed in Debug interaction.
  std::array<bool, window::kKeyCount> panels_held_{};
};

}  // namespace augusta::debug

#endif  // AUGUSTA_DEBUG_INTERACTION_H_
