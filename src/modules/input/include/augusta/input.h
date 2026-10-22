#ifndef AUGUSTA_INPUT_H_
#define AUGUSTA_INPUT_H_

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>

#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/window_events.h"

/// \file
/// augusta::input turns the window's device events into player input: the
/// controls and the keymap that binds them to the window's keys, and what the
/// events accumulate into, sampled as one tick's worth of intent, a
/// command::Command (ARCHITECTURE.md §5's "Input handling").
///
/// It links the window only for its events' type (window_events.h, ADR-0059):
/// the client Runtime drains the window with Window::Poll on the Main/Render
/// thread and hands the events to Input::Handle, and neither the window nor the
/// Renderer knows anything of input. Input is responsible for the thread-safe
/// accumulation until the Prediction thread - a different thread, on the
/// server's fixed tick rate (ADR-0005) - samples it.
///
/// The Command it samples lives in the shared core (augusta::command), not
/// here: the server screens, queues and simulates Commands but never samples
/// a device, so it does not link this module.
namespace augusta::input {

/// The key that releases the captured cursor. Reserved: no control may be bound to it.
inline constexpr window::Key kReleaseCursorKey = window::Key::kEscape;

/// key's name as augustac.yaml spells it: "W", "7", "LeftShift", "Space", "F12", "MouseRight".
[[nodiscard]] std::string_view NameOf(window::Key key);

/// The key with this name (NameOf), or nullopt if none has it. Case-sensitive.
[[nodiscard]] std::optional<window::Key> KeyNamed(std::string_view name);

/// What a player does, as opposed to which key does it: Input::Sample reads
/// controls, and a Keymap says which key triggers each.
enum class Control {
  kMoveForward,
  kMoveBack,
  kMoveLeft,
  kMoveRight,
  kSprint,
  kCrouch,
  kProne,
  /// Held: WeaponHandling turns it into shots at the weapon's rate (US-07).
  kFire,
  /// Held: aim down sights (US-06).
  kAds,
  /// Pressed: one reload per press, however long it is held (US-08). Keep
  /// last: kControlCount counts from it.
  kReload,
};

/// How many controls Control names.
inline constexpr std::size_t kControlCount = static_cast<std::size_t>(Control::kReload) + 1;

/// control's name as augustac.yaml's `keys` section spells it: "move_forward", "sprint", ...
[[nodiscard]] std::string_view NameOf(Control control);

/// The control with this name (NameOf), or nullopt if none has it.
[[nodiscard]] std::optional<Control> ControlNamed(std::string_view name);

/// Which key triggers each control, indexed by Control. Each control has its
/// own key, never kReleaseCursorKey; augusta::config checks both.
using Keymap = std::array<window::Key, kControlCount>;

/// The bindings a player gets without a `keys` section: WASD, LeftShift sprints,
/// LeftControl crouches, Z goes prone, the left mouse button fires, the right
/// one aims down sights and R reloads.
inline constexpr Keymap kDefaultKeymap = {
    window::Key::kW,          window::Key::kS,           window::Key::kA, window::Key::kD,
    window::Key::kLeftShift,  window::Key::kLeftControl, window::Key::kZ, window::Key::kMouseLeft,
    window::Key::kMouseRight, window::Key::kR,
};

/// Default view rotation, in radians, per pixel of mouse movement (see
/// Config::mouse_sensitivity).
constexpr float kDefaultMouseSensitivity = 0.0022F;

/// Tunable input-response values, data rather than mechanism (ARCHITECTURE.md
/// §8's mechanism/policy/data split, same category as physics::StaminaConfig).
struct Config {
  /// View rotation, in radians, per pixel of mouse movement.
  float mouse_sensitivity = kDefaultMouseSensitivity;
  /// Which key triggers each control.
  Keymap keymap = kDefaultKeymap;
};

/// The steepest pitch, up or down, Input lets the view reach, in radians: just
/// short of straight up/down.
inline constexpr float kMaxLookPitch = 1.55F;

/// Where the player aims now: the yaw and pitch of its view, as a Command's,
/// and whether it holds ADS.
struct Aim {
  float yaw = 0.0F;
  float pitch = 0.0F;
  bool ads = false;
};

/// Accumulates the window's events the client Runtime hands it and samples
/// them into a command::Command once per Prediction tick. The client
/// constructs exactly one.
class Input {
 public:
  explicit Input(const Config& config);

  /// Builds this tick's Command from the controls held (through the keymap)
  /// and the view accumulated so far. The move controls go forward/back/left/
  /// right of where the view faces across the ground (opposite controls cancel,
  /// a diagonal is no faster); sprint, crouch, prone, fire and ADS are held, and
  /// prone wins over crouch; reload is true on the one Sample after its key was
  /// pressed, even if it was already released again. Call once per Prediction
  /// tick, from the Prediction thread (ADR-0005) - safe to call
  /// concurrently with Handle, which the Main/Render thread calls.
  [[nodiscard]] command::Command Sample();

  /// Where the player aims now: the yaw and pitch accumulated so far, mouse
  /// movement since the last Sample included, and whether ADS is held. The
  /// camera turns and zooms by it every render frame, from the Main/Render
  /// thread, rather than by the Command's, which is only as new as the last tick.
  [[nodiscard]] Aim CurrentAim() const;

  /// Whether control's key is held now, between Samples: what a render frame
  /// reads a control by, from the Main/Render thread (a spectator's fire).
  [[nodiscard]] bool IsHeld(Control control) const;

  /// Whether the cursor should be captured for mouselook: true at first, false
  /// once kReleaseCursorKey is pressed, and true again on the next click of any
  /// mouse button (which is taken by the capture, not passed on as a control).
  /// While released, the
  /// game has neither mouse nor keyboard: the view does not turn, every held
  /// key is let go, and keys pressed meanwhile are ignored. The caller
  /// applies it (window::Window::SetCursorLocked), from the Main/Render thread.
  [[nodiscard]] bool CursorCaptured() const;

  /// Takes the window's events, oldest first, as Window::Poll returns them,
  /// from the Main/Render thread. A key event presses or lets go of a key; a
  /// cursor move turns the view by how far the cursor moved since the previous
  /// one, and the first only sets where the cursor starts.
  void Handle(std::span<const window::Event> events);

  /// Lets go of every key and button held, as if each were released, the
  /// capture unchanged: the next Sample has no move, fire or ADS, and a key
  /// still down only counts again once pressed again. The next cursor position
  /// starts a new baseline rather than turning the view, as events the caller
  /// keeps from Input meanwhile may have moved the cursor. A reload pressed
  /// since the last Sample still reloads. From the Main/Render thread, safe
  /// beside a concurrent Sample.
  void LetGo();

  /// Releases the cursor exactly as pressing kReleaseCursorKey does (see
  /// CursorCaptured): every held control is let go, and the next click
  /// captures it again. From the Main/Render thread, safe beside a concurrent
  /// Sample.
  void ReleaseCursor();

 private:
  // One event of Handle's; callers hold mutex_.
  void Apply(const window::KeyEvent& event);
  void Apply(const window::MouseMoveEvent& event);
  // Whether control's key is held down; callers hold mutex_.
  [[nodiscard]] bool Held(Control control) const;
  // Captures or releases the cursor; callers hold mutex_.
  void SetCursorCaptured(bool captured);

  float mouse_sensitivity_;
  Keymap keymap_;
  mutable std::mutex mutex_;
  std::array<bool, window::kKeyCount> held_{};
  std::optional<window::MouseMoveEvent> last_cursor_;
  float yaw_ = 0.0F;
  float pitch_ = 0.0F;
  bool cursor_captured_ = true;
  // The reload key was pressed since the last Sample.
  bool reload_pressed_ = false;
};

}  // namespace augusta::input

#endif  // AUGUSTA_INPUT_H_
