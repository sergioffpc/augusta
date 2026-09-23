#include "augusta/input.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <numbers>

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

physics::Stance DesiredStance(bool crouch, bool prone) {
  if (prone) {
    return physics::Stance::kProne;
  }
  return crouch ? physics::Stance::kCrouching : physics::Stance::kStanding;
}

}  // namespace

math::Quat ViewRotation(float yaw, float pitch) {
  return glm::angleAxis(yaw, math::Vec3(0.0F, 1.0F, 0.0F)) * glm::angleAxis(pitch, math::Vec3(1.0F, 0.0F, 0.0F));
}

Input::Input(const Config& config) : mouse_sensitivity_(config.mouse_sensitivity) {}

Command Input::Sample() {
  const std::lock_guard<std::mutex> lock(mutex_);
  Command command;
  const float forward = Axis(Held(Key::kW), Held(Key::kS));
  const float right = Axis(Held(Key::kD), Held(Key::kA));
  command.movement.direction = MovementDirection(forward, right, yaw_);
  command.movement.sprint = Held(Key::kLeftShift);
  command.movement.desired_stance = DesiredStance(Held(Key::kLeftControl), Held(Key::kZ));
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
  if (!cursor_captured_) {
    return;
  }
  held_.at(static_cast<std::size_t>(event.key)) = event.action == Action::kPressed;
  if (event.key == Key::kEscape && event.action == Action::kPressed) {
    SetCursorCaptured(false);
  }
}

void Input::OnMouseButtonEvent(const MouseButtonEvent& event) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!cursor_captured_ && event.action == Action::kPressed) {
    SetCursorCaptured(true);
  }
  // Fire and ADS arrive with combat (US-06, US-07, M4).
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
  // Capturing or releasing moves the cursor, so the next position it reports
  // starts a new baseline instead of turning the view.
  last_cursor_.reset();
}

bool Input::Held(Key key) const { return held_.at(static_cast<std::size_t>(key)); }

}  // namespace augusta::input
