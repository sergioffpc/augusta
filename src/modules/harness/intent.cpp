#include "augusta/intent.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/weapon.h"

namespace augusta::harness {

namespace {

// One callable from a lambda per alternative, for std::visit.
template <typename... Lambdas>
struct Overloaded : Lambdas... {
  using Lambdas::operator()...;
};

// A MoveTo has arrived once its body is this near its point along the floor,
// in metres: more than a sprint covers in one tick at any tick rate a server
// runs, so it never steps over its point and back.
constexpr float kArrivedM = 0.5F;
// A MoveTo makes progress when its body comes this much nearer its point than
// it has been, in metres; it is Blocked after a second of ticks without.
constexpr float kProgressM = 0.1F;

// Where a body's eye and torso are, as a fraction of its height in its stance:
// estimates, but near enough to aim by at the ranges a Map holds. The Shot
// itself leaves from the character's real eye (ADR-0040).
constexpr float kEyeHeightFraction = 0.9F;
constexpr float kTorsoHeightFraction = 0.6F;

// FireBursts holds the trigger this many ticks, then releases it this many: a
// short Burst, then enough rest for the Recoil offset to come back down.
constexpr int kBurstTicks = 6;
constexpr int kReleaseTicks = 18;

math::Vec3 AlongTheFloor(const math::Vec3& from, const math::Vec3& to) { return {to.x - from.x, 0.0F, to.z - from.z}; }

// The point at height_fraction of body's height in its stance, above its feet.
math::Vec3 PointUp(const physics::BodyState& body, float height_fraction) {
  return body.position + math::Vec3(0.0F, physics::StanceHeight(body.stance) * height_fraction, 0.0F);
}

// The view that looks from from toward to (command::Command's conventions:
// yaw 0 looks down -Z and turns left as it grows, pitch looks up as it grows),
// its pitch short of straight up or down as a player's is.
Look LookAt(const math::Vec3& from, const math::Vec3& to) {
  const math::Vec3 toward = to - from;
  const float pitch = std::atan2(toward.y, std::hypot(toward.x, toward.z));
  return Look{.yaw = std::atan2(-toward.x, -toward.z),
              .pitch = std::clamp(pitch, -input::kMaxLookPitch, input::kMaxLookPitch)};
}

// The body of entity in the newest Authoritative State of view, if it is there.
std::optional<physics::BodyState> BodyOf(const ServerView& view, EntityId entity) {
  if (!view.authoritative.has_value()) {
    return std::nullopt;
  }
  const auto& bodies = view.authoritative->bodies;
  const auto found = std::ranges::find(bodies, entity, &EntityBody::entity);
  return found == bodies.end() ? std::nullopt : std::optional(found->body);
}

// Empties held, keeping its ending callback in ended under ending if it held an Intent.
template <typename Held, typename Ended>
void EndInto(std::optional<Held>& held, IntentEnding ending, std::vector<Ended>& ended) {
  if (held.has_value()) {
    ended.push_back({.on_ended = std::move(held->on_ended), .ending = ending});
    held.reset();
  }
}

// Tells each of ended how it ended, once the lock they were taken under is let go.
template <typename Ended>
void NotifyEnded(const std::vector<Ended>& ended) {
  for (const Ended& each : ended) {
    if (each.on_ended) {
      each.on_ended(each.ending);
    }
  }
}

}  // namespace

template <typename Intent>
void IntentExecutor::Replace(std::optional<Held<Intent>>& channel,
                             std::type_identity_t<std::optional<Held<Intent>>> next) {
  std::vector<Ended> ended;
  {
    const std::scoped_lock lock(mutex_);
    EndInto(raw_, IntentEnding::kReplaced, ended);
    EndInto(channel, IntentEnding::kReplaced, ended);
    channel = std::move(next);
  }
  NotifyEnded(ended);
}

void IntentExecutor::SetMovement(MovementIntent intent, OnIntentEnded on_ended) {
  Replace(movement_, Held<MovementIntent>{.intent = std::move(intent), .on_ended = std::move(on_ended)});
}

void IntentExecutor::SetAim(AimIntent intent, OnIntentEnded on_ended) {
  Replace(aim_, Held<AimIntent>{.intent = std::move(intent), .on_ended = std::move(on_ended)});
}

void IntentExecutor::SetTrigger(TriggerIntent intent, OnIntentEnded on_ended) {
  Replace(trigger_, Held<TriggerIntent>{.intent = std::move(intent), .on_ended = std::move(on_ended)});
}

void IntentExecutor::ClearMovement() { Replace(movement_, {}); }

void IntentExecutor::ClearAim() { Replace(aim_, {}); }

void IntentExecutor::ClearTrigger() { Replace(trigger_, {}); }

void IntentExecutor::SetRaw(const command::Command& command, OnIntentEnded on_ended) {
  std::vector<Ended> ended;
  {
    const std::scoped_lock lock(mutex_);
    EndInto(raw_, IntentEnding::kReplaced, ended);
    EndInto(movement_, IntentEnding::kReplaced, ended);
    EndInto(aim_, IntentEnding::kReplaced, ended);
    EndInto(trigger_, IntentEnding::kReplaced, ended);
    raw_ = Held<command::Command>{.intent = command, .on_ended = std::move(on_ended)};
  }
  NotifyEnded(ended);
}

std::optional<IntentEnding> IntentExecutor::Carry(Held<MovementIntent>& held, const ServerView& view,
                                                  const physics::BodyState& own, command::Command& command) {
  Tracking& tracking = held.tracking;
  return std::visit(Overloaded{[&](const Move& move) -> std::optional<IntentEnding> {
                                 command.movement = {
                                     .direction = move.direction, .sprint = move.sprint, .desired_stance = move.stance};
                                 return std::nullopt;
                               },
                               [&](const MoveTo& move_to) -> std::optional<IntentEnding> {
                                 command.movement.desired_stance = move_to.stance;
                                 const math::Vec3 toward = AlongTheFloor(own.position, move_to.point);
                                 const float distance = math::Length(toward);
                                 if (distance <= kArrivedM) {
                                   return IntentEnding::kArrived;
                                 }
                                 if (distance < tracking.nearest - kProgressM) {
                                   tracking.nearest = distance;
                                   tracking.ticks_without_progress = 0;
                                 } else if (++tracking.ticks_without_progress >= int{view.accepted->tick_rate_hz}) {
                                   // NextCommand carries an Intent out only once the server has admitted it.
                                   return IntentEnding::kBlocked;
                                 }
                                 command.movement.direction = toward / distance;
                                 command.movement.sprint = move_to.sprint;
                                 return std::nullopt;
                               }},
                    held.intent);
}

bool IntentExecutor::TargetGone(const AimAt& aim_at, const Tracking& tracking, const ServerView& view) {
  if (!tracking.match.has_value()) {
    return false;
  }
  if (*tracking.match != view.matches_started || !view.in_match || std::ranges::contains(view.dead, aim_at.target)) {
    return true;
  }
  return view.authoritative.has_value() && !BodyOf(view, aim_at.target).has_value();
}

void IntentExecutor::EndAimIfTargetGone(const ServerView& view, std::vector<Ended>& ended) {
  if (!aim_.has_value()) {
    return;
  }
  const AimAt* aim_at = std::get_if<AimAt>(&aim_->intent);
  if (aim_at != nullptr && TargetGone(*aim_at, aim_->tracking, view)) {
    EndInto(aim_, IntentEnding::kTargetGone, ended);
  }
}

std::optional<IntentEnding> IntentExecutor::Carry(Held<AimIntent>& held, const ServerView& view,
                                                  const physics::BodyState& own, command::Command& command) {
  Tracking& tracking = held.tracking;
  return std::visit(Overloaded{[&](const Look& look) -> std::optional<IntentEnding> {
                                 command.yaw = look.yaw;
                                 command.pitch = look.pitch;
                                 return std::nullopt;
                               },
                               [&](const AimAt& aim_at) -> std::optional<IntentEnding> {
                                 if (!tracking.match.has_value()) {
                                   tracking.match = view.matches_started;
                                 }
                                 if (TargetGone(aim_at, tracking, view)) {
                                   return IntentEnding::kTargetGone;
                                 }
                                 const std::optional<physics::BodyState> target = BodyOf(view, aim_at.target);
                                 if (!target.has_value()) {
                                   // No Authoritative State yet to place it by.
                                   return std::nullopt;
                                 }
                                 // From where its prediction puts it, as a client aims from where it
                                 // shows its own player: the newest state's is a round trip behind.
                                 const Look look =
                                     LookAt(PointUp(own, kEyeHeightFraction), PointUp(*target, kTorsoHeightFraction));
                                 command.yaw = look.yaw;
                                 command.pitch = look.pitch;
                                 return std::nullopt;
                               }},
                    held.intent);
}

std::optional<IntentEnding> IntentExecutor::Carry(Held<TriggerIntent>& held, const ServerView& view,
                                                  const physics::BodyState& /*own*/, command::Command& command) {
  Tracking& tracking = held.tracking;
  return std::visit(Overloaded{[&](const HoldFire& /*hold*/) -> std::optional<IntentEnding> {
                                 command.fire = true;
                                 return std::nullopt;
                               },
                               [&](const Reload& /*reload*/) -> std::optional<IntentEnding> {
                                 command.reload = true;
                                 return IntentEnding::kDone;
                               },
                               [&](const FireBursts& /*bursts*/) -> std::optional<IntentEnding> {
                                 if (view.authoritative.has_value() && view.authoritative->rifle.rounds == 0) {
                                   // Pressed once, not again while the reload is under way.
                                   command.reload = view.authoritative->rifle.reload_remaining <= 0.0F;
                                   tracking.trigger_ticks = 0;
                                   return std::nullopt;
                                 }
                                 command.fire = tracking.trigger_ticks % (kBurstTicks + kReleaseTicks) < kBurstTicks;
                                 ++tracking.trigger_ticks;
                                 return std::nullopt;
                               }},
                    held.intent);
}

template <typename Intent>
void IntentExecutor::CarryChannel(std::optional<Held<Intent>>& channel, const ServerView& view,
                                  const physics::BodyState& own, command::Command& command, std::vector<Ended>& ended) {
  if (!channel.has_value()) {
    return;
  }
  if (const std::optional<IntentEnding> ending = Carry(*channel, view, own, command); ending.has_value()) {
    EndInto(channel, *ending, ended);
  }
}

command::Command IntentExecutor::NextCommand(const ServerView& view, const physics::BodyState& own) {
  std::vector<Ended> ended;
  command::Command command;
  {
    const std::scoped_lock lock(mutex_);
    command.yaw = last_view_.yaw;
    command.pitch = last_view_.pitch;
    command.movement.desired_stance = last_stance_;
    if (view.OwnAlive()) {
      if (raw_.has_value()) {
        command = raw_->intent;
        // Sent once: the Raw held from here on reloads no more.
        raw_->intent.reload = false;
      } else {
        CarryChannel(movement_, view, own, command, ended);
        CarryChannel(aim_, view, own, command, ended);
        CarryChannel(trigger_, view, own, command, ended);
      }
      last_view_ = Look{.yaw = command.yaw, .pitch = command.pitch};
      last_stance_ = command.movement.desired_stance;
    } else {
      // Its Intents wait, but a target that dies or leaves meanwhile is gone
      // now, not once this Agent is back in a Match.
      EndAimIfTargetGone(view, ended);
    }
  }
  command.seen_tick = view.authoritative.has_value() ? view.authoritative->tick : 0;
  command.seen_fraction = 0.0F;
  NotifyEnded(ended);
  return command;
}

}  // namespace augusta::harness
