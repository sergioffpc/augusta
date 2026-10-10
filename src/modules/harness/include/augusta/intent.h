#ifndef AUGUSTA_INTENT_H_
#define AUGUSTA_INTENT_H_

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/math.h"
#include "augusta/physics.h"

/// \file
/// Intents (CONTEXT.md, ADR-0052): what an Agent is doing on each of its three
/// channels - movement, aim, trigger - carried out every tick as the source of
/// its Runner's next Command (RunnerHooks::next_command), so the one choosing
/// them - a script, a test - decides at its own pace and never on a tick.
/// Neither the Runner nor the Session knows of them, and nothing here knows of
/// Python: a script's bindings and the C++ tests set the same Intents.
///
/// Each channel holds at most one Intent, set or cleared on its own; a Raw
/// Intent holds one whole Command on all three at once. Whoever sets an Intent
/// may be told once how it ended: Replaced when its channel is set again or
/// cleared, Done when a one-shot has been sent.
namespace augusta::harness {

/// How an Intent ended; each is told at most one.
enum class IntentEnding : std::uint8_t {
  /// Its channel was set again or cleared, or a Raw took it.
  kReplaced,
  /// A one-shot (Reload) was sent.
  kDone,
};

/// Movement: hold a direction, with sprint and stance (physics::MovementInput).
struct Move {
  /// In world space; need not be normalized. The zero vector stands still.
  math::Vec3 direction{};
  bool sprint = false;
  physics::Stance stance = physics::Stance::kStanding;
};

/// Aim: hold a view, in radians (command::Command's yaw and pitch).
struct Look {
  float yaw = 0.0F;
  float pitch = 0.0F;
};

/// Trigger: hold the trigger, as full-auto fire.
struct HoldFire {};

/// Trigger: reload once, then end Done.
struct Reload {};

using MovementIntent = std::variant<Move>;
using AimIntent = std::variant<Look>;
using TriggerIntent = std::variant<HoldFire, Reload>;

/// Told how an Intent ended: on the thread that set or cleared its channel for
/// Replaced, on NextCommand's (the Prediction thread) for any other ending, so
/// it must return at once and must not wait on anything a tick waits for.
/// Endings told on two threads can arrive in either order. Never called with
/// the executor's lock held, so it may set another Intent.
using OnIntentEnded = std::function<void(IntentEnding)>;

/// One Agent's channels, and the Command they come to each tick. Setting and
/// clearing are safe from any thread and never wait for a tick; NextCommand is
/// called from one thread at a time (the Runner's Prediction thread).
class IntentExecutor {
 public:
  void SetMovement(MovementIntent intent, OnIntentEnded on_ended = {});
  void SetAim(AimIntent intent, OnIntentEnded on_ended = {});
  void SetTrigger(TriggerIntent intent, OnIntentEnded on_ended = {});
  /// Takes all three channels: command is sent every tick until a channel is
  /// set or cleared, which empties the others. Its reload is sent on one tick
  /// only, a rising edge, as the server expects; its Seen time is ignored.
  void SetRaw(const command::Command& command, OnIntentEnded on_ended = {});

  void ClearMovement();
  void ClearAim();
  void ClearTrigger();

  /// The Command for the coming tick, from what the server has told this
  /// Agent as of view and own, its body as its prediction last left it. While
  /// its player is not alive in a Match, nothing it sent would be played: the
  /// Command moves, fires and reloads not, and the Intents wait. With no aim,
  /// the view is the last one sent; with no movement, the stance is. The Seen
  /// time is always the newest Authoritative State's, as this Agent sees the
  /// others.
  [[nodiscard]] command::Command NextCommand(const ServerView& view, const physics::BodyState& own);

 private:
  template <typename Intent>
  struct Held {
    Intent intent;
    OnIntentEnded on_ended;
  };
  // An Intent that has ended, to be told once the lock is let go.
  struct Ended {
    OnIntentEnded on_ended;
    IntentEnding ending{};
  };

  // Ends the Raw and the Intent on channel Replaced, and holds next there.
  template <typename Intent>
  void Replace(std::optional<Held<Intent>>& channel, std::type_identity_t<std::optional<Held<Intent>>> next);
  // Writes what the channels hold into command, and what ends on this tick
  // into ended. The caller holds the lock.
  void ApplyChannels(command::Command& command, std::vector<Ended>& ended);

  std::mutex mutex_;
  std::optional<Held<MovementIntent>> movement_;
  std::optional<Held<AimIntent>> aim_;
  std::optional<Held<TriggerIntent>> trigger_;
  std::optional<Held<command::Command>> raw_;
  // What the last Command sent held, for the ticks no Intent says otherwise.
  Look last_view_;
  physics::Stance last_stance_ = physics::Stance::kStanding;
};

}  // namespace augusta::harness

#endif  // AUGUSTA_INTENT_H_
