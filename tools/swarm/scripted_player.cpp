#include "scripted_player.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/intent.h"
#include "augusta/math.h"
#include "augusta/physics.h"

namespace augusta::swarm {

namespace {

// How far it may stray from where the Match spawned it, in metres, before it
// heads back: it knows nothing of the Map's edges, and this keeps it on any Map
// whose floor reaches a few metres around each spawn point.
constexpr float kLeashM = 3.0F;

// A leg lasts a whole number of seconds from one to three; of every ten legs,
// this many sprint, and this many stand and this many crouch (the rest go prone).
constexpr std::uint32_t kMinLegSeconds = 1;
constexpr std::uint32_t kLegSecondsChoices = 3;
constexpr std::uint32_t kOutOf = 10;
constexpr std::uint32_t kSprinting = 3;
constexpr std::uint32_t kStanding = 7;
constexpr std::uint32_t kCrouching = 2;

// How many values mt19937 draws from, as a double: what maps a draw onto [0, 1).
constexpr double kDrawRange = static_cast<double>(std::numeric_limits<std::uint32_t>::max()) + 1.0;
constexpr double kFullTurn = 2.0 * std::numbers::pi;

// The stance a draw of 0 to kOutOf - 1 picks.
physics::Stance StanceFor(std::uint32_t draw) {
  if (draw < kStanding) {
    return physics::Stance::kStanding;
  }
  if (draw < kStanding + kCrouching) {
    return physics::Stance::kCrouching;
  }
  return physics::Stance::kProne;
}

bool HasBody(const harness::AuthoritativeState& state, harness::EntityId entity) {
  return std::ranges::contains(state.bodies, entity, &harness::EntityBody::entity);
}

// The living player nearest own, other than own, or nullopt if there is none.
std::optional<harness::EntityId> NearestTarget(const harness::ServerView& view, harness::EntityId own,
                                               const physics::BodyState& own_body) {
  std::optional<harness::EntityId> nearest;
  float nearest_distance = std::numeric_limits<float>::max();
  for (const harness::EntityBody& other : view.authoritative->bodies) {
    if (other.entity == own || std::ranges::contains(view.dead, other.entity)) {
      continue;
    }
    const float distance = math::Length(other.body.position - own_body.position);
    if (distance < nearest_distance) {
      nearest = other.entity;
      nearest_distance = distance;
    }
  }
  return nearest;
}

// Where the Match view tells of spawned this player, if it does.
std::optional<math::Vec3> OwnSpawn(const harness::ServerView& view) {
  if (!view.match_start.has_value() || !view.accepted.has_value()) {
    return std::nullopt;
  }
  const auto& players = view.match_start->players;
  const auto found = std::ranges::find(players, view.accepted->session, &harness::MatchPlayer::session);
  return found == players.end() ? std::nullopt : std::optional(found->spawn);
}

}  // namespace

ScriptedPlayer::ScriptedPlayer(std::uint32_t seed) : random_(seed) {}

void ScriptedPlayer::StartMatchIfNew(std::uint32_t matches_started) {
  if (matches_started == match_) {
    return;
  }
  // The last Match's leg and way back lead nowhere in this one.
  match_ = matches_started;
  leg_.ticks_left = 0;
  heading_back_ = false;
}

void ScriptedPlayer::StartLegIfDone(std::uint8_t tick_rate_hz) {
  if (heading_back_ || leg_.ticks_left > 0) {
    return;
  }
  const double turn = static_cast<double>(random_()) / kDrawRange;
  leg_.yaw = static_cast<float>((turn * kFullTurn) - std::numbers::pi);
  leg_.sprint = random_() % kOutOf < kSprinting;
  leg_.stance = StanceFor(random_() % kOutOf);
  const std::uint32_t seconds = kMinLegSeconds + (random_() % kLegSecondsChoices);
  leg_.ticks_left = static_cast<int>(seconds * tick_rate_hz);
  intents_.SetMovement(
      harness::Move{.direction = command::ViewDirection(leg_.yaw, 0.0F), .sprint = leg_.sprint, .stance = leg_.stance});
}

void ScriptedPlayer::HeadBackIfStrayed(const harness::ServerView& view, const physics::BodyState& own) {
  const std::optional<math::Vec3> spawn = OwnSpawn(view);
  if (!spawn.has_value()) {
    return;
  }
  const math::Vec3 away = own.position - *spawn;
  const bool strayed = std::hypot(away.x, away.z) > kLeashM;
  if (heading_back_) {
    // Back within reach, the next leg is drawn as any other.
    heading_back_ = strayed;
    return;
  }
  if (!strayed) {
    return;
  }
  // Ending the leg under way, until it is back within reach or can get no nearer.
  intents_.SetMovement(harness::MoveTo{.point = *spawn, .sprint = leg_.sprint, .stance = leg_.stance},
                       [this](harness::IntentEnding ending) {
                         if (ending != harness::IntentEnding::kReplaced) {
                           heading_back_ = false;
                         }
                       });
  heading_back_ = true;
  leg_.ticks_left = 0;
}

void ScriptedPlayer::TakeOnNearest(const harness::ServerView& view, harness::EntityId own_entity,
                                   const physics::BodyState& own) {
  const std::optional<harness::EntityId> target = NearestTarget(view, own_entity, own);
  if (!target.has_value()) {
    if (firing_) {
      intents_.ClearTrigger();
      firing_ = false;
    }
    return;
  }
  if (aimed_at_ != target) {
    intents_.SetAim(harness::AimAt{.target = *target}, [this](harness::IntentEnding ending) {
      if (ending != harness::IntentEnding::kReplaced) {
        aimed_at_.reset();
      }
    });
    aimed_at_ = target;
  }
  if (!firing_) {
    intents_.SetTrigger(harness::FireBursts{});
    firing_ = true;
  }
}

command::Command ScriptedPlayer::NextCommand(const harness::ServerView& view, const physics::BodyState& own) {
  // Outside a Match and once dead, its Intents wait, holding nothing.
  if (view.OwnAlive() && view.authoritative.has_value()) {
    // OwnAlive holds only once the server has admitted this player and named its body.
    const harness::EntityId own_entity = *view.OwnEntity();
    if (HasBody(*view.authoritative, own_entity)) {
      StartMatchIfNew(view.matches_started);
      StartLegIfDone(view.accepted->tick_rate_hz);
      --leg_.ticks_left;
      HeadBackIfStrayed(view, own);
      TakeOnNearest(view, own_entity, own);
    }
  }
  return intents_.NextCommand(view, own);
}

}  // namespace augusta::swarm
