#include "augusta/assets.h"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/math.h"

namespace augusta::assets {

std::string_view BodyPartName(BodyPart part) {
  switch (part) {
    case BodyPart::kHead:
      return "head";
    case BodyPart::kTorso:
      return "torso";
    case BodyPart::kLimb:
      return "limb";
  }
  return "unknown";
}

std::optional<BodyPart> FirstMissingBodyPart(std::span<const HitboxData> hitboxes) {
  for (const BodyPart part : {BodyPart::kHead, BodyPart::kTorso, BodyPart::kLimb}) {
    if (std::ranges::none_of(hitboxes, [part](const HitboxData& hitbox) { return hitbox.part == part; })) {
      return part;
    }
  }
  return std::nullopt;
}

std::expected<std::vector<JointHitbox>, UnresolvedJoint> BindHitboxes(const Skeleton& skeleton,
                                                                      std::vector<HitboxData> hitboxes) {
  std::vector<JointHitbox> bound;
  bound.reserve(hitboxes.size());
  for (HitboxData& hitbox : hitboxes) {
    const auto joint = std::ranges::find(skeleton.joints, hitbox.joint, &JointData::name);
    if (joint == skeleton.joints.end()) {
      return std::unexpected(UnresolvedJoint{.joint = std::move(hitbox.joint)});
    }
    bound.push_back(
        {.joint = static_cast<std::uint32_t>(joint - skeleton.joints.begin()), .hitbox = std::move(hitbox)});
  }
  return bound;
}

bool IsValidAssetType(std::uint8_t value) {
  switch (static_cast<AssetType>(value)) {
    case AssetType::kMesh:
    case AssetType::kTexture:
    case AssetType::kAudio:
    case AssetType::kCollision:
    case AssetType::kSpawnPoint:
    case AssetType::kHitbox:
    case AssetType::kScene:
    case AssetType::kScript:
    case AssetType::kCharacters:
    case AssetType::kEye:
    case AssetType::kCueSound:
    case AssetType::kSkeleton:
    case AssetType::kAnimationSkeleton:
    case AssetType::kClip:
    case AssetType::kStanceTransitions:
    case AssetType::kRifle:
    case AssetType::kWeaponSocket:
      return true;
  }
  return false;
}

std::string SanitizePrimPath(std::string_view usd_prim_path) {
  if (!usd_prim_path.empty() && usd_prim_path.front() == '/') {
    usd_prim_path.remove_prefix(1);
  }
  return std::string(usd_prim_path);
}

// SceneData lists parents before children (ADR-0032), so one forward pass sees
// each parent's transform already done.
std::vector<math::Mat4> ComputeWorldTransforms(const SceneData& scene) {
  std::vector<math::Mat4> world;
  world.reserve(scene.nodes.size());
  for (const SceneNode& node : scene.nodes) {
    const math::Mat4 local = math::ToMat4(node.translation, node.rotation, node.scale);
    world.push_back(node.parent_index == kSceneNodeNoParent ? local : world[node.parent_index] * local);
  }
  return world;
}

}  // namespace augusta::assets
