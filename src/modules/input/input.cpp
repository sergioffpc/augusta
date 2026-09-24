#include "augusta/input.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <numbers>
#include <optional>
#include <string_view>

#include "augusta/math.h"
#include "augusta/physics.h"

namespace augusta::input {

namespace {

// +1, -1 or 0: which of two opposite keys wins, if either.
float Axis(bool positive, bool negative) { return (positive ? 1.0F : 0.0F) - (negative ? 1.0F : 0.0F); }

constexpr float kFullTurn = 2.0F * std::numbers::pi_v<float>;

// yaw within (-pi, pi], the same heading.
float WrapYaw(float yaw) {
  const float wrapped = std::remainder(yaw, kFullTurn);
  return wrapped <= -std::numbers::pi_v<float> ? wrapped + kFullTurn : wrapped;
}

// The world-space direction of forward/right input (each -1, 0 or +1) for a
// view turned by yaw, unit length unless both are 0: the view looks down -Z
// before it turns, so forward is -Z and right is +X.
math::Vec3 MovementDirection(float forward, float right, float yaw) {
  return math::Normalize(ViewRotation(yaw, 0.0F) * math::Vec3(right, 0.0F, -forward));
}

// Every key's name, in Key's order.
constexpr std::array<std::string_view, kKeyCount> kKeyNames = {
    "A",           "B",         "C",          "D",           "E",
    "F",           "G",         "H",          "I",           "J",
    "K",           "L",         "M",          "N",           "O",
    "P",           "Q",         "R",          "S",           "T",
    "U",           "V",         "W",          "X",           "Y",
    "Z",           "0",         "1",          "2",           "3",
    "4",           "5",         "6",          "7",           "8",
    "9",           "Space",     "Tab",        "Enter",       "Backspace",
    "Escape",      "LeftShift", "RightShift", "LeftControl", "RightControl",
    "LeftAlt",     "RightAlt",  "Up",         "Down",        "Left",
    "Right",       "F1",        "F2",         "F3",          "F4",
    "F5",          "F6",        "F7",         "F8",          "F9",
    "F10",         "F11",       "F12",        "MouseLeft",   "MouseRight",
    "MouseMiddle",
};

// Added keys go before kMouseMiddle, and their names before its name.
static_assert(kKeyNames.back() == "MouseMiddle");

// Every control's name, in Control's order.
constexpr std::array<std::string_view, kControlCount> kControlNames = {
    "move_forward", "move_back", "move_left", "move_right", "sprint", "crouch", "prone", "fire", "ads", "reload",
};

// Added controls go before kReload, and their names before its name.
static_assert(kControlNames.back() == "reload");

// The enumerator whose name in names is name, if any.
template <typename Enum, std::size_t Count>
std::optional<Enum> Named(const std::array<std::string_view, Count>& names, std::string_view name) {
  const auto found = std::ranges::find(names, name);
  if (found == names.end()) {
    return std::nullopt;
  }
  return static_cast<Enum>(found - names.begin());
}

physics::Stance DesiredStance(bool crouch, bool prone) {
  if (prone) {
    return physics::Stance::kProne;
  }
  return crouch ? physics::Stance::kCrouching : physics::Stance::kStanding;
}

}  // namespace

bool IsMouseButton(Key key) { return key == Key::kMouseLeft || key == Key::kMouseRight || key == Key::kMouseMiddle; }

std::string_view NameOf(Key key) { return kKeyNames.at(static_cast<std::size_t>(key)); }

std::optional<Key> KeyNamed(std::string_view name) { return Named<Key>(kKeyNames, name); }

std::string_view NameOf(Control control) { return kControlNames.at(static_cast<std::size_t>(control)); }

std::optional<Control> ControlNamed(std::string_view name) { return Named<Control>(kControlNames, name); }

math::Quat ViewRotation(float yaw, float pitch) {
  return glm::angleAxis(yaw, math::Vec3(0.0F, 1.0F, 0.0F)) * glm::angleAxis(pitch, math::Vec3(1.0F, 0.0F, 0.0F));
}

Input::Input(const Config& config) : mouse_sensitivity_(config.mouse_sensitivity), keymap_(config.keymap) {}

Command Input::Sample() {
  const std::lock_guard<std::mutex> lock(mutex_);
  Command command;
  const float forward = Axis(Held(Control::kMoveForward), Held(Control::kMoveBack));
  const float right = Axis(Held(Control::kMoveRight), Held(Control::kMoveLeft));
  command.movement.direction = MovementDirection(forward, right, yaw_);
  command.movement.sprint = Held(Control::kSprint);
  command.movement.desired_stance = DesiredStance(Held(Control::kCrouch), Held(Control::kProne));
  command.fire = Held(Control::kFire);
  command.ads = Held(Control::kAds);
  command.reload = reload_pressed_;
  reload_pressed_ = false;
  command.yaw = yaw_;
  command.pitch = pitch_;
  return command;
}

bool Input::CursorCaptured() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return cursor_captured_;
}

void Input::OnKeyEvent(const KeyEvent& event) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const bool pressed = event.state == KeyState::kPressed;
  if (!cursor_captured_) {
    // Released, the game has no keyboard; a mouse click only takes the cursor back.
    if (pressed && IsMouseButton(event.key)) {
      SetCursorCaptured(true);
    }
    return;
  }
  held_.at(static_cast<std::size_t>(event.key)) = pressed;
  if (pressed && event.key == keymap_.at(static_cast<std::size_t>(Control::kReload))) {
    reload_pressed_ = true;
  }
  if (pressed && event.key == kReleaseCursorKey) {
    SetCursorCaptured(false);
  }
}

void Input::OnMouseMoveEvent(const MouseMoveEvent& event) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!cursor_captured_) {
    return;
  }
  if (last_cursor_.has_value()) {
    // Screen x grows rightward and a rightward turn is a negative yaw; screen
    // y grows downward and looking up is a positive pitch.
    yaw_ = WrapYaw(yaw_ - ((event.x - last_cursor_->x) * mouse_sensitivity_));
    pitch_ = std::clamp(pitch_ - ((event.y - last_cursor_->y) * mouse_sensitivity_), -kMaxLookPitch, kMaxLookPitch);
  }
  last_cursor_ = event;
}

void Input::SetCursorCaptured(bool captured) {
  cursor_captured_ = captured;
  // Released, the game has no keyboard: nothing stays held, and a key only
  // counts again once pressed after the capture returns.
  held_.fill(false);
  reload_pressed_ = false;
  // Capturing or releasing moves the cursor, so the next position it reports
  // starts a new baseline instead of turning the view.
  last_cursor_.reset();
}

bool Input::Held(Control control) const {
  return held_.at(static_cast<std::size_t>(keymap_.at(static_cast<std::size_t>(control))));
}

}  // namespace augusta::input
