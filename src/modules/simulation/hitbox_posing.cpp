#include "hitbox_posing.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/simulation.h"

namespace augusta::simulation {

namespace {

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

float ExtentOf(const hitbox::CharacterHitbox& hitbox) {
  float extent = 0.0F;
  for (const ballistics::Triangle& triangle : hitbox.triangles) {
    for (const math::Vec3& point : {triangle.a, triangle.b, triangle.c}) {
      extent = std::max(extent, math::Length(point));
    }
  }
  return extent;
}

Reach::Reach(std::span<const hitbox::CharacterHitbox> hitboxes) {
  extents_.reserve(hitboxes.size());
  for (const hitbox::CharacterHitbox& hitbox : hitboxes) {
    extents_.push_back(Extent{.joint = hitbox.joint, .radius = ExtentOf(hitbox)});
  }
}

// A point radius from a Joint's origin is no farther from it in the world than
// radius times the Joint's largest stretch, which its linear part's Frobenius
// norm bounds whatever scale a Skeleton composes into it.
float Reach::Of(const math::Vec3& root, std::span<const math::Mat4> joints) const {
  float reach = 0.0F;
  for (const Extent& extent : extents_) {
    assert(extent.joint < joints.size());
    const math::Mat4& joint = joints[extent.joint];
    const math::Vec3 origin(joint[3]);
    const float stretch = std::sqrt(math::Dot(math::Vec3(joint[0]), math::Vec3(joint[0])) +
                                    math::Dot(math::Vec3(joint[1]), math::Vec3(joint[1])) +
                                    math::Dot(math::Vec3(joint[2]), math::Vec3(joint[2])));
    reach = std::max(reach, math::Length(origin - root) + (extent.radius * stretch));
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

bool HitboxPosing::Reaches(const math::Vec3& center, float reach) const {
  const ballistics::Segment& segment = bullet_segment_;
  const math::Vec3 along = segment.to - segment.from;
  const float length_squared = math::Dot(along, along);
  const float fraction =
      length_squared > 0.0F ? std::clamp(math::Dot(center - segment.from, along) / length_squared, 0.0F, 1.0F) : 0.0F;
  const math::Vec3 apart = center - (segment.from + (along * fraction));
  return math::Dot(apart, apart) <= reach * reach;
}

void HitboxPosing::Consider(EntityId target, std::span<const hitbox::CharacterHitbox> hitboxes, float reach,
                            const hitbox::Pose& pose, std::span<const math::Mat4> joints) {
  if (!Reaches(pose.position, reach)) {
    return;
  }
  // Posed now if no bullet of this tick has asked for them at this moment yet.
  const auto [posing, added] = posings_.try_emplace(PosingKey{.target = target, .time = bullet_time_},
                                                    Posing{.first = posed_hitboxes_.size(), .count = hitboxes.size()});
  if (added) {
    for (const hitbox::PlacedHitbox& placed : hitbox::Place(hitboxes, joints)) {
      posed_hitboxes_.push_back(PosedHitbox{.struck = {.target = target, .part = placed.part},
                                            .first = posed_triangles_.size(),
                                            .count = placed.triangles.size()});
      posed_triangles_.insert(posed_triangles_.end(), placed.triangles.begin(), placed.triangles.end());
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
