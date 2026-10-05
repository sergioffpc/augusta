#include "scripted_player.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/weapon.h"

namespace augusta::loadtest {

namespace {

// Where a body's eye and torso are, as a fraction of its height in its stance:
// estimates, but near enough to aim by at the ranges a Map holds. The Shot
// itself leaves from the character's real eye (ADR-0040).
constexpr float kEyeHeightFraction = 0.9F;
constexpr float kTorsoHeightFraction = 0.6F;

// The trigger is held this many ticks, then released this many: a short Burst,
// then enough rest for the Recoil offset to come back down.
constexpr int kBurstTicks = 6;
constexpr int kReleaseTicks = 18;

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

std::optional<physics::BodyState> BodyOf(const harness::AuthoritativeState& state, harness::EntityId entity) {
  const auto found = std::ranges::find(state.bodies, entity, &harness::EntityBody::entity);
  return found == state.bodies.end() ? std::nullopt : std::optional(found->body);
}

// The point at height_fraction of body's height in its stance, above its feet.
math::Vec3 PointUp(const physics::BodyState& body, float height_fraction) {
  return body.position + math::Vec3(0.0F, physics::StanceHeight(body.stance) * height_fraction, 0.0F);
}

// The body of the living player nearest own, other than own, or nullopt if there is none.
std::optional<physics::BodyState> NearestTarget(const harness::ServerView& view, harness::EntityId own,
                                                const physics::BodyState& own_body) {
  std::optional<physics::BodyState> nearest;
  float nearest_distance = std::numeric_limits<float>::max();
  for (const harness::EntityBody& other : view.authoritative->bodies) {
    if (other.entity == own || std::ranges::contains(view.dead, other.entity)) {
      continue;
    }
    const float distance = math::Length(other.body.position - own_body.position);
    if (distance < nearest_distance) {
      nearest = other.body;
      nearest_distance = distance;
    }
  }
  return nearest;
}

struct View {
  float yaw = 0.0F;
  float pitch = 0.0F;
};

// The view that looks from from toward to (command::Command's conventions:
// yaw 0 looks down -Z and turns left as it grows, pitch looks up as it grows),
// its pitch short of straight up or down as a player's is.
View LookAt(const math::Vec3& from, const math::Vec3& to) {
  const math::Vec3 toward = to - from;
  const float pitch = std::atan2(toward.y, std::hypot(toward.x, toward.z));
  return View{.yaw = std::atan2(-toward.x, -toward.z),
              .pitch = std::clamp(pitch, -input::kMaxLookPitch, input::kMaxLookPitch)};
}

}  // namespace

ScriptedPlayer::ScriptedPlayer(std::uint32_t seed) : random_(seed) {}

void ScriptedPlayer::StartLegIfDone(std::uint8_t tick_rate_hz) {
  if (leg_.ticks_left > 0) {
    return;
  }
  const double turn = static_cast<double>(random_()) / kDrawRange;
  leg_.yaw = static_cast<float>((turn * kFullTurn) - std::numbers::pi);
  leg_.sprint = random_() % kOutOf < kSprinting;
  leg_.stance = StanceFor(random_() % kOutOf);
  const std::uint32_t seconds = kMinLegSeconds + (random_() % kLegSecondsChoices);
  leg_.ticks_left = static_cast<int>(seconds * tick_rate_hz);
}

command::Command ScriptedPlayer::NextCommand(const harness::ServerView& view, const physics::BodyState& own) {
  if (!view.OwnAlive() || !view.authoritative.has_value()) {
    return command::Command{};
  }
  // OwnAlive holds only once the server has admitted this player and named its body.
  const harness::EntityId own_entity = *view.OwnEntity();
  if (!BodyOf(*view.authoritative, own_entity).has_value()) {
    return command::Command{};
  }

  StartLegIfDone(view.accepted->tick_rate_hz);
  --leg_.ticks_left;
  command::Command command;
  command.movement = physics::MovementInput{
      .direction = command::ViewDirection(leg_.yaw, 0.0F), .sprint = leg_.sprint, .desired_stance = leg_.stance};
  command.yaw = leg_.yaw;
  // A Scripted player sees the other players where the newest state puts them.
  command.seen_tick = view.authoritative->tick;

  const std::optional<physics::BodyState> target = NearestTarget(view, own_entity, own);
  if (!target.has_value()) {
    trigger_ticks_ = 0;
    return command;
  }
  // From where its prediction puts it, as a client aims from where it shows its
  // own player: the newest state's is a round trip behind.
  const View aim = LookAt(PointUp(own, kEyeHeightFraction), PointUp(*target, kTorsoHeightFraction));
  command.yaw = aim.yaw;
  command.pitch = aim.pitch;

  const weapon::State& rifle = view.authoritative->rifle;
  if (rifle.rounds == 0) {
    command.reload = rifle.reload_remaining <= 0.0F;
    trigger_ticks_ = 0;
    return command;
  }
  command.fire = trigger_ticks_ % (kBurstTicks + kReleaseTicks) < kBurstTicks;
  ++trigger_ticks_;
  return command;
}

}  // namespace augusta::loadtest
