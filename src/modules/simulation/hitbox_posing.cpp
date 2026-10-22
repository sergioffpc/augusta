#include "hitbox_posing.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>
#include <utility>

#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"
#include "hitbox_history.h"

namespace augusta::simulation {

namespace {

// What ReachOf adds to the farthest point of a character's hitboxes: far above
// the rounding of posing them, or of Reaches, anywhere on the position grid, so
// culling by it never drops a hitbox a bullet crosses.
constexpr float kReachMargin = 0.1F;

// point, of a standing character's root space, where it is on a body in pose:
// lowered for its stance as the eye is, turned about the vertical as the view
// is, then moved to the body.
math::Vec3 PosePoint(const math::Vec3& point, const Pose& pose) {
  return pose.position + (command::ViewRotation(pose.yaw, 0.0F) * physics::LowerToStance(point, pose.stance));
}

// Whether segment passes within reach of center.
bool Reaches(const ballistics::Segment& segment, const math::Vec3& center, float reach) {
  const math::Vec3 along = segment.to - segment.from;
  const float length_squared = math::Dot(along, along);
  const float fraction =
      length_squared > 0.0F ? std::clamp(math::Dot(center - segment.from, along) / length_squared, 0.0F, 1.0F) : 0.0F;
  const math::Vec3 apart = center - (segment.from + (along * fraction));
  return math::Dot(apart, apart) <= reach * reach;
}

// A posed hitbox's Surface id: its place in the tick's posed hitboxes, counted
// from one past kMapSurface, so no posed hitbox ever shares the Map's id.
physics::SurfaceId SurfaceOf(std::size_t posed_hitbox) {
  return static_cast<physics::SurfaceId>(std::to_underlying(kMapSurface) + 1 + posed_hitbox);
}

// The place in the tick's posed hitboxes of a struck Surface that is not the
// Map: the inverse of SurfaceOf.
std::size_t PosedHitboxOf(physics::SurfaceId surface) {
  assert(surface != kMapSurface);
  return std::to_underlying(surface) - std::to_underlying(kMapSurface) - 1;
}

}  // namespace

// Measured standing: any other stance only lowers a point
// (physics::LowerToStance), which brings it no farther from the root.
float ReachOf(std::span<const CharacterHitbox> hitboxes) {
  float reach = 0.0F;
  for (const CharacterHitbox& hitbox : hitboxes) {
    for (const ballistics::Triangle& triangle : hitbox.triangles) {
      for (const math::Vec3& point : {triangle.a, triangle.b, triangle.c}) {
        reach = std::max(reach, math::Length(point));
      }
    }
  }
  return reach + kReachMargin;
}

void HitboxPosing::Clear() {
  posed_triangles_.clear();
  posed_hitboxes_.clear();
  posings_.clear();
}

void HitboxPosing::BeginBullet(double time, const ballistics::Segment& segment) {
  bullet_time_ = time;
  bullet_segment_ = segment;
  bullet_posings_.clear();
  bullet_targets_.clear();
}

void HitboxPosing::Consider(EntityId target, std::span<const CharacterHitbox> standing, float reach, const Pose& pose) {
  if (!Reaches(bullet_segment_, pose.position, reach)) {
    return;
  }
  // Posed now if no bullet of this tick has asked for them at this moment yet.
  const auto [posing, added] = posings_.try_emplace(PosingKey{.target = target, .time = bullet_time_},
                                                    Posing{.first = posed_hitboxes_.size(), .count = standing.size()});
  if (added) {
    for (const CharacterHitbox& hitbox : standing) {
      posed_hitboxes_.push_back(PosedHitbox{.struck = {.target = target, .part = hitbox.part},
                                            .first = posed_triangles_.size(),
                                            .count = hitbox.triangles.size()});
      for (const ballistics::Triangle& triangle : hitbox.triangles) {
        posed_triangles_.push_back(ballistics::Triangle{
            .a = PosePoint(triangle.a, pose), .b = PosePoint(triangle.b, pose), .c = PosePoint(triangle.c, pose)});
      }
    }
  }
  bullet_posings_.push_back(posing->second);
  bullet_targets_.push_back(PosedTarget{.target = target, .pose = pose});
}

std::span<const ballistics::Hitbox> HitboxPosing::BulletHitboxes() {
  // Only now, with every triangle in place, can the hitboxes view them.
  bullet_hitboxes_.clear();
  const std::span<const ballistics::Triangle> triangles(posed_triangles_);
  for (const Posing& posing : bullet_posings_) {
    for (std::size_t index = posing.first; index < posing.first + posing.count; ++index) {
      const PosedHitbox& hitbox = posed_hitboxes_[index];
      bullet_hitboxes_.push_back(
          ballistics::Hitbox{.surface = SurfaceOf(index), .triangles = triangles.subspan(hitbox.first, hitbox.count)});
    }
  }
  return bullet_hitboxes_;
}

StruckHitbox HitboxPosing::Struck(physics::SurfaceId surface) const {
  // at() keeps a broken id from reading past the posed hitboxes.
  return posed_hitboxes_.at(PosedHitboxOf(surface)).struck;
}

}  // namespace augusta::simulation
