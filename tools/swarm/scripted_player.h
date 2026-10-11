#ifndef AUGUSTA_SWARM_SCRIPTED_PLAYER_H_
#define AUGUSTA_SWARM_SCRIPTED_PLAYER_H_

#include <cstdint>
#include <optional>
#include <random>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/intent.h"
#include "augusta/physics.h"

/// \file
/// The Scripted player (CONTEXT.md): what plays in a person's place, deciding
/// what to do from the Server view and its own prediction, so a load or
/// end-to-end test can fill a server with no one at the keyboard. It decides
/// only, and carries its decisions out through Intents (augusta/intent.h), as
/// an Agent's script does: augusta-swarm (main.cpp) hands each one's Commands
/// to a harness::Runner, which predicts and sends them like any client's.
///
/// In a Match it wanders, a leg at a time: a random heading, sprint and stance
/// for one to three seconds (Move), never more than a few metres from where the
/// Match spawned it, since it knows nothing of the Map's edges: strayed, it
/// heads back there (MoveTo). It aims at the nearest living other player
/// (AimAt) and fires at it in Bursts (FireBursts), reloading once its magazine
/// is empty. Every random choice comes from its seed, so the same seed makes
/// the same choices from the same Server views.
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

  // Starts over, with no leg under way, once a new Match has started.
  void StartMatchIfNew(std::uint32_t matches_started);
  // Sets a new leg of tick_rate_hz-tick seconds moving, once the one under way is done.
  void StartLegIfDone(std::uint8_t tick_rate_hz);
  // Heads own straight back toward where the Match spawned this player, if it
  // has strayed beyond its leash, until it is back there.
  void HeadBackIfStrayed(const harness::ServerView& view, const physics::BodyState& own);
  // Aims and fires at the nearest living other player, or holds fire if there is none.
  void TakeOnNearest(const harness::ServerView& view, harness::EntityId own_entity, const physics::BodyState& own);

  // The standard fixes mt19937's sequence, unlike its distributions', so every
  // platform draws the same numbers from the same seed; the draws are mapped
  // onto choices by hand for the same reason.
  std::mt19937 random_;
  Leg leg_;
  // The Match its decisions are for (ServerView::matches_started).
  std::uint32_t match_ = 0;
  // What its Intents carry out, so it sets each only when it changes: setting
  // one again would start it over.
  bool heading_back_ = false;
  std::optional<harness::EntityId> aimed_at_;
  bool firing_ = false;
  // Last, so it is gone before the state its Intents' endings write to.
  harness::IntentExecutor intents_;
};

}  // namespace augusta::swarm

#endif  // AUGUSTA_SWARM_SCRIPTED_PLAYER_H_
