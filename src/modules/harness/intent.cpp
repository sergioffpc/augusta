#include "augusta/intent.h"

#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/physics.h"

namespace augusta::harness {

namespace {

// One callable from a lambda per alternative, for std::visit.
template <typename... Lambdas>
struct Overloaded : Lambdas... {
  using Lambdas::operator()...;
};

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

void IntentExecutor::ApplyChannels(command::Command& command, std::vector<Ended>& ended) {
  if (movement_.has_value()) {
    std::visit(Overloaded{[&](const Move& move) {
                 command.movement = {.direction = move.direction, .sprint = move.sprint, .desired_stance = move.stance};
               }},
               movement_->intent);
  }
  if (aim_.has_value()) {
    std::visit(Overloaded{[&](const Look& look) {
                 command.yaw = look.yaw;
                 command.pitch = look.pitch;
               }},
               aim_->intent);
  }
  if (trigger_.has_value()) {
    // Whether the Intent is over once this tick is sent.
    const bool over = std::visit(Overloaded{[&](const HoldFire& /*hold*/) {
                                              command.fire = true;
                                              return false;
                                            },
                                            [&](const Reload& /*reload*/) {
                                              command.reload = true;
                                              return true;
                                            }},
                                 trigger_->intent);
    if (over) {
      EndInto(trigger_, IntentEnding::kDone, ended);
    }
  }
}

command::Command IntentExecutor::NextCommand(const ServerView& view, const physics::BodyState& /*own*/) {
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
        ApplyChannels(command, ended);
      }
      last_view_ = Look{.yaw = command.yaw, .pitch = command.pitch};
      last_stance_ = command.movement.desired_stance;
    }
  }
  command.seen_tick = view.authoritative.has_value() ? view.authoritative->tick : 0;
  command.seen_fraction = 0.0F;
  NotifyEnded(ended);
  return command;
}

}  // namespace augusta::harness
