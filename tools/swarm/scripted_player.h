#ifndef AUGUSTA_SWARM_SCRIPTED_PLAYER_H_
#define AUGUSTA_SWARM_SCRIPTED_PLAYER_H_

#include <cstdint>
#include <random>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/physics.h"

/// \file
/// The Scripted player (CONTEXT.md): what plays in a person's place, deciding
/// each tick's Command from the Server view and its own prediction, so a load or end-to-end test
/// can fill a server with no one at the keyboard. It decides only: augusta-swarm
/// (main.cpp) hands each one's Commands to a harness::Runner, which predicts
/// and sends them like any client's.
///
/// In a Match it wanders, a leg at a time: a random heading, sprint and stance
/// for one to three seconds, never more than a few metres from where the Match
/// spawned it, since it knows nothing of the Map's edges. It aims at the nearest living other player, as
/// the newest Authoritative State places them, from where its own prediction
/// places its own body, as a client aims, and fires at it in short
/// Bursts, releasing the trigger between them so the Recoil offset recovers.
/// It reloads once its magazine is empty. Every random choice comes from its
/// seed, so the same seed makes the same choices from the same Server views.
namespace augusta::swarm {

class ScriptedPlayer {
 public:
  explicit ScriptedPlayer(std::uint32_t seed);

  /// The Command for the coming tick, from what the server has told this player
  /// as of view and own, its body as its prediction last left it: nothing held
  /// outside a Match and once its player is dead. Called once per tick, from
  /// one thread at a time (the Runner's Prediction thread).
  [[nodiscard]] command::Command NextCommand(const harness::ServerView& view, const physics::BodyState& own);

 private:
  // One stretch of wandering: where it heads, how, and for how many more ticks.
  struct Leg {
    float yaw = 0.0F;
    bool sprint = false;
    physics::Stance stance = physics::Stance::kStanding;
    int ticks_left = 0;
  };

  // A new leg of tick_rate_hz-tick seconds, once the one under way is done.
  void StartLegIfDone(std::uint8_t tick_rate_hz);
  // Turns the leg under way straight back toward where the Match spawned this
  // player, if own has strayed beyond its leash, and ends it there.
  void HeadBackIfStrayed(const harness::ServerView& view, const physics::BodyState& own);

  // The standard fixes mt19937's sequence, unlike its distributions', so every
  // platform draws the same numbers from the same seed; the draws are mapped
  // onto choices by hand for the same reason.
  std::mt19937 random_;
  Leg leg_;
  // How many ticks the trigger has cycled since there was last nothing to fire
  // at or no round to fire: where in its Burst-and-release cycle the next tick is.
  int trigger_ticks_ = 0;
};

}  // namespace augusta::swarm

#endif  // AUGUSTA_SWARM_SCRIPTED_PLAYER_H_
