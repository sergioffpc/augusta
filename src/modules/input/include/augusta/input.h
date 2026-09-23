#ifndef AUGUSTA_INPUT_H_
#define AUGUSTA_INPUT_H_

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>

#include "augusta/math.h"
#include "augusta/physics.h"

// augusta::input owns the vocabulary of player input, from raw device
// events up to one tick's worth of intent, and turns the former into the
// latter (ARCHITECTURE.md §7's "Input handling - reads device input,
// hands commands to PredictionWorld").
//
// The raw device-event shapes (KeyEvent, MouseButtonEvent, MouseMoveEvent)
// and the EventSink that receives them live here rather than in
// augusta::renderer, even though Renderer is the one calling EventSink:
// Renderer needs this module (to know what to push and who to push it
// to), so this module cannot also need Renderer's header (to implement
// something declared there) without a circular module dependency. Since
// these shapes are conceptually input's vocabulary anyway (not a
// rendering concern - Renderer only sees them because Falcor happens to
// fuse window/device delivery with the GPU device it also owns, see
// ADR-0009), the dependency runs one way: Renderer depends on Input, not
// the reverse. augusta::input::Input is the sole implementation of
// EventSink; Renderer pushes events to it from the Main/Render thread as
// they occur, and Input is responsible for the thread-safe accumulation
// until the Simulation thread - a different thread, a different fixed
// tick rate (ADR-0005) - samples it.
//
// Command is deliberately also usable without a live Input instance: the
// server links no Falcor/GLFW code and never constructs augusta::input::
// Input, but still needs the same struct's layout to deserialize what a
// client sent, before Input Validation (US-15) checks it and
// SimulationWorld's own CommandIngestion phase consumes it - see
// ARCHITECTURE.md's Shared Core / Networking Protocol. Until augusta::
// networking's own interface is designed, this is Command's home too.
namespace augusta::input {

// A physical key this engine's gameplay currently binds to something.
// Deliberately not a full keyboard enumeration (contrast Falcor's own
// ~100-entry Key) - extend as new gameplay needs a new key, same
// restraint as physics::Stance only naming the stances US-04 needs.
enum class Key {
  kW,
  kA,
  kS,
  kD,
  kLeftShift,    // Sprint (US-04/US-05).
  kLeftControl,  // Crouch (US-04).
  kZ,            // Prone (US-04).
  kEscape,       // Releases the captured cursor.
  kR,            // Reload (US-08). Keep last: kKeyCount counts from it.
};

/// How many keys Key names.
inline constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::kR) + 1;

// A physical mouse button this engine's gameplay currently binds to
// something.
enum class MouseButton {
  kLeft,   // Fire (US-07).
  kRight,  // Toggle ADS (US-06).
};

// Whether a key or button transitioned to pressed or released this
// event. Held-down state is not reported here - Input derives it itself
// by tracking Pressed/Released pairs.
enum class Action {
  kPressed,
  kReleased,
};

// One key's press/release transition.
struct KeyEvent {
  Key key;
  Action action;
};

// One mouse button's press/release transition.
struct MouseButtonEvent {
  MouseButton button;
  Action action;
};

// The cursor's position within the client area, in pixels, at the time
// of one Move event. (0, 0) is the top-left corner. Carries an absolute
// position, not a delta - Input computes a delta from consecutive events
// itself, the same way Falcor's own MouseEvent::Move works underneath.
struct MouseMoveEvent {
  float x = 0.0F;
  float y = 0.0F;
};

// Receives keyboard/mouse events as Renderer forwards them from Falcor,
// on the Main/Render thread. Input is the only implementation; Renderer
// holds a reference to one, supplied at construction, and never queries
// it back - purely a push destination. The referenced EventSink must
// outlive the Renderer it is given to.
class EventSink {
 public:
  virtual ~EventSink() = default;

  virtual void OnKeyEvent(const KeyEvent& event) = 0;
  virtual void OnMouseButtonEvent(const MouseButtonEvent& event) = 0;
  virtual void OnMouseMoveEvent(const MouseMoveEvent& event) = 0;
};

// Default view rotation, in radians, per pixel of mouse movement (see
// Config::mouse_sensitivity).
constexpr float kDefaultMouseSensitivity = 0.0022F;

// Tunable input-response values, data rather than mechanism (ARCHITECTURE.md
// §8's mechanism/policy/data split, same category as physics::StaminaConfig).
struct Config {
  // View rotation, in radians, per pixel of mouse movement.
  float mouse_sensitivity = kDefaultMouseSensitivity;
};

/// The steepest pitch, up or down, Input lets the view reach, in radians: just
/// short of straight up/down.
inline constexpr float kMaxLookPitch = 1.55F;

/// The rotation of a view with this yaw and pitch (see Command): yaw about +Y,
/// then pitch about the view's own +X. Applied to -Z, it gives where the view
/// looks.
[[nodiscard]] math::Quat ViewRotation(float yaw, float pitch);

// One tick's worth of player intent. Built by Input::Sample on the
// client; deserialized off the wire on the server (see above).
struct Command {
  // Movement direction, sprint, and desired stance for this tick -
  // passed straight through to physics::World::Step's MovementInput.
  physics::MovementInput movement;
  // View orientation for this tick, in radians, accumulated from mouse
  // movement. Yaw 0 looks down -Z, the renderer camera's forward, and a
  // positive yaw turns left (counter-clockwise seen from above, right-handed
  // about +Y); Input keeps it within one turn. A positive pitch looks up;
  // Input clamps it to kMaxLookPitch either way, short of straight up/down
  // (no gimbal flip). ViewRotation turns the pair into a rotation. Determines
  // aim direction for WeaponHandling (bullet origin/direction, US-07) as well
  // as view for Camera (US-06).
  float yaw = 0.0F;
  float pitch = 0.0F;
  // True while the aim-down-sights control is held (US-06). Hip-fire is
  // the default (false).
  bool ads = false;
  // True while the fire control is held (US-07). WeaponHandling, not
  // Input, turns a held fire control into discrete shots at the
  // weapon's fire rate - Input only reports raw intent.
  bool fire = false;
  // True on exactly the one tick the reload control was pressed
  // (US-08) - a rising edge, not a held state, regardless of how long
  // the control is actually held.
  bool reload = false;
};

// Accumulates device state pushed via EventSink and samples it into a
// Command once per Simulation tick. The client constructs exactly one,
// alongside the one Renderer, and wires the two together (Renderer
// needs this as its EventSink; this needs nothing from Renderer in
// return - the coupling is one-directional).
class Input : public EventSink {
 public:
  explicit Input(const Config& config);

  // Builds this tick's Command from the keys held and the view accumulated
  // so far. WASD move forward/back/left/right of where the view faces across
  // the ground (opposite keys cancel, a diagonal is no faster), Shift held
  // sprints, Ctrl held crouches and Z held goes prone, winning over Ctrl.
  // Fire, ADS and reload are not sampled yet (M4). Call once per Simulation
  // tick, from the Simulation thread (ARCHITECTURE.md §8) - safe to call
  // concurrently with the OnXxx methods below, which arrive from
  // Renderer::PumpEvents on the Main/Render thread.
  [[nodiscard]] Command Sample();

  // Whether the cursor should be captured for mouselook: true at first, false
  // once Escape is pressed, and true again on the next mouse click (which is
  // taken by the capture, not passed on as a control). While released, the
  // game has neither mouse nor keyboard: the view does not turn, every held
  // key is let go, and keys pressed meanwhile are ignored. The caller
  // applies it (Renderer::SetCursorLocked), from the Main/Render thread.
  [[nodiscard]] bool CursorCaptured() const;

  // EventSink - see there for when/why these are called. Not meant to
  // be called directly by anything other than Renderer (and tests).
  void OnKeyEvent(const KeyEvent& event) override;
  void OnMouseButtonEvent(const MouseButtonEvent& event) override;
  // Turns the view by how far the cursor moved since the previous event; the
  // first event only sets where the cursor starts.
  void OnMouseMoveEvent(const MouseMoveEvent& event) override;

 private:
  // Whether key is held down; callers hold mutex_.
  [[nodiscard]] bool Held(Key key) const;
  // Captures or releases the cursor; callers hold mutex_.
  void SetCursorCaptured(bool captured);

  float mouse_sensitivity_;
  mutable std::mutex mutex_;
  std::array<bool, kKeyCount> held_{};
  std::optional<MouseMoveEvent> last_cursor_;
  float yaw_ = 0.0F;
  float pitch_ = 0.0F;
  bool cursor_captured_ = true;
};

}  // namespace augusta::input

#endif  // AUGUSTA_INPUT_H_
