#ifndef AUGUSTA_TESTS_CLIPS_FIXTURE_H_
#define AUGUSTA_TESTS_CLIPS_FIXTURE_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/base/maths/quaternion.h>
#include <ozz/base/maths/transform.h>
#include <ozz/base/maths/vec_float.h>

#include "augusta/assets.h"
#include "augusta/clips.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/weapon.h"

// A Character's Clips authored in memory for a test (ADR-0066), with
// ozz-animation's offline libraries, so a test never depends on the shipped
// Character's art: RestClips, still Clips that hold any Skeleton in its Rest
// pose, each locomotion Clip with the stride length and duration the test gives
// it, and Rig, which arms a Skeleton for them; the Walker, whose Clips move its
// Joints, for a test of a pose; and the Holder, whose Aim offset and ADS grip
// bend and raise it while it holds the rifle. Every one has a weapon socket and
// the left arm it reaches the rifle with, as every Character does.
namespace augusta::clips_fixture {

// Standing locomotion strides, one second each, so each Clip moves as fast as
// its stride: forward at Movement's walk (3 m/s), sprint at its sprint (4.8 m/s).
inline constexpr float kStandingForward = 3.0F;
inline constexpr float kStandingBackward = 2.0F;
inline constexpr float kStandingLeft = 2.2F;
inline constexpr float kStandingRight = 2.4F;
inline constexpr float kStandingSprint = 4.8F;
inline constexpr float kCrouchingForward = 1.5F;
inline constexpr float kProneForward = 0.6F;
inline constexpr float kProneBackward = 0.5F;

// The foot contacts of a one-second Clip with a stride: its left foot lands at
// its start and its right halfway. None without one.
inline std::vector<assets::FootContact> StepsOf(float stride_length) {
  if (stride_length <= 0.0F) {
    return {};
  }
  return {{.foot = assets::Foot::kLeft, .ratio = 0.0F}, {.foot = assets::Foot::kRight, .ratio = 0.5F}};
}

// The ozz-animation RawSkeleton Joint of skeleton's joint, in its Rest pose,
// its children under it.
inline ozz::animation::offline::RawSkeleton::Joint RawJointOf(const assets::Skeleton& skeleton, std::uint32_t joint) {
  const assets::JointData& data = skeleton.joints.at(joint);
  ozz::animation::offline::RawSkeleton::Joint raw;
  raw.name = data.name;
  raw.transform.translation = ozz::math::Float3(data.translation.x, data.translation.y, data.translation.z);
  raw.transform.rotation = ozz::math::Quaternion(data.rotation.x, data.rotation.y, data.rotation.z, data.rotation.w);
  raw.transform.scale = ozz::math::Float3(data.scale.x, data.scale.y, data.scale.z);
  for (std::uint32_t child = 0; child < skeleton.joints.size(); ++child) {
    if (skeleton.joints[child].parent == joint) {
      raw.children.push_back(RawJointOf(skeleton, child));
    }
  }
  return raw;
}

// Clips that hold skeleton still in its Rest pose, its weapon socket socket:
// each stance's, standing's sprint, prone with no sides, and its air Clip, each
// one second long, each locomotion Clip with the strides above.
inline std::shared_ptr<const animation::CharacterClips> RestClips(const assets::Skeleton& skeleton,
                                                                  const animation::WeaponSocket& socket) {
  ozz::animation::offline::RawSkeleton raw;
  for (std::uint32_t joint = 0; joint < skeleton.joints.size(); ++joint) {
    if (skeleton.joints[joint].parent == assets::kJointNoParent) {
      raw.roots.push_back(RawJointOf(skeleton, joint));
    }
  }
  auto animated = ozz::animation::offline::SkeletonBuilder()(raw);
  EXPECT_TRUE(animated);

  auto clips = std::make_shared<animation::CharacterClips>();
  clips->skeleton = std::move(*animated);
  for (const char* const name : clips->skeleton.joint_names()) {
    const auto found = std::ranges::find(skeleton.joints, std::string_view(name), &assets::JointData::name);
    clips->joints.push_back(static_cast<std::uint32_t>(found - skeleton.joints.begin()));
  }
  const auto still = [&](float stride_length) {
    ozz::animation::offline::RawAnimation animation;
    animation.duration = 1.0F;
    for (const std::uint32_t joint : clips->joints) {
      const assets::JointData& data = skeleton.joints.at(joint);
      ozz::animation::offline::RawAnimation::JointTrack track;
      track.translations.push_back(
          {.time = 0.0F, .value = ozz::math::Float3(data.translation.x, data.translation.y, data.translation.z)});
      track.rotations.push_back(
          {.time = 0.0F,
           .value = ozz::math::Quaternion(data.rotation.x, data.rotation.y, data.rotation.z, data.rotation.w)});
      track.scales.push_back({.time = 0.0F, .value = ozz::math::Float3(data.scale.x, data.scale.y, data.scale.z)});
      animation.tracks.push_back(std::move(track));
    }
    auto built = ozz::animation::offline::AnimationBuilder()(animation);
    EXPECT_TRUE(built);
    return animation::Clip{
        .animation = std::move(*built), .stride_length = stride_length, .foot_contacts = StepsOf(stride_length)};
  };
  const auto stance = [&](float forward, float backward, float left, float right, bool sides) {
    animation::StanceClips stance_clips;
    stance_clips.idle = still(0.0F);
    stance_clips.forward = still(forward);
    stance_clips.backward = still(backward);
    stance_clips.aim_down = still(0.0F);
    stance_clips.aim_center = still(0.0F);
    stance_clips.aim_up = still(0.0F);
    stance_clips.ads = still(0.0F);
    if (sides) {
      stance_clips.left = still(left);
      stance_clips.right = still(right);
    }
    return stance_clips;
  };
  animation::StanceClips standing = stance(kStandingForward, kStandingBackward, kStandingLeft, kStandingRight, true);
  standing.sprint = still(kStandingSprint);
  clips->stances[static_cast<std::size_t>(physics::Stance::kStanding)] = std::move(standing);
  clips->stances[static_cast<std::size_t>(physics::Stance::kCrouching)] =
      stance(kCrouchingForward, 1.2F, 1.3F, 1.3F, true);
  clips->stances[static_cast<std::size_t>(physics::Stance::kProne)] =
      stance(kProneForward, kProneBackward, 0.0F, 0.0F, false);
  clips->air = still(0.0F);
  clips->transitions = {.standing_crouching = 0.25F, .standing_prone = 0.8F, .crouching_prone = 0.6F};
  clips->weapon_socket = socket;
  return clips;
}

// Where Rig hangs a left arm, from its skeleton's first Joint: a shoulder up to
// the left, then 0.5 m down to the elbow and 0.5 m on to the hand.
inline constexpr float kRigShoulderSide = 0.25F;
inline constexpr float kRigShoulderHeight = 1.45F;
inline constexpr float kRigBone = 0.5F;
// Where Rig's weapon socket holds the rifle's Grip by default, in the
// skeleton's root space at rest: 0.3 m ahead of its root, so the left hand
// reaches across to it.
inline constexpr float kRigGripAhead = 0.3F;

// Rigs skeleton, a test Character's that has no Clips of its own, to hold a
// rifle: appends a left arm (LeftArm, LeftForearm, LeftHand) under its first
// Joint, and returns RestClips of it with the weapon socket on right_hand,
// holding the rifle's Grip at grip, in root space at rest, unturned: the rifle
// points ahead, along a level view, wherever the view looks.
inline std::shared_ptr<const animation::CharacterClips> Rig(assets::Skeleton& skeleton, std::uint32_t right_hand = 0,
                                                            const math::Vec3& grip = math::Vec3(0.0F, 0.0F,
                                                                                                -kRigGripAhead)) {
  const std::string& root = skeleton.joints.front().name;
  const math::Quat none(1.0F, 0.0F, 0.0F, 0.0F);
  const std::array<assets::JointData, 3> arm{{
      {.name = root + "/LeftArm", .parent = 0, .translation = math::Vec3(-kRigShoulderSide, kRigShoulderHeight, 0.0F)},
      {.name = root + "/LeftArm/LeftForearm", .parent = 0, .translation = math::Vec3(0.0F, -kRigBone, 0.0F)},
      {.name = root + "/LeftArm/LeftForearm/LeftHand", .parent = 0, .translation = math::Vec3(0.0F, -kRigBone, 0.0F)},
  }};
  for (std::size_t bone = 0; bone < arm.size(); ++bone) {
    assets::JointData joint = arm.at(bone);
    if (bone > 0) {
      joint.parent = static_cast<std::uint32_t>(skeleton.joints.size() - 1);
    }
    const math::Mat4 local = math::ToMat4(joint.translation, none, math::Vec3(1.0F));
    skeleton.rest_transforms.push_back(skeleton.rest_transforms.at(joint.parent) * local);
    skeleton.joints.push_back(std::move(joint));
  }
  const animation::WeaponSocket socket{
      .right_hand = right_hand,
      .frame = math::Inverse(skeleton.rest_transforms.at(right_hand)) * math::ToMat4(grip, none, math::Vec3(1.0F)),
      .left_hand = static_cast<std::uint32_t>(skeleton.joints.size() - 1)};
  return RestClips(skeleton, socket);
}

// A Character's Clips for a test that needs some but no pose of its own: Rig's,
// of a Skeleton of a root Joint alone.
inline std::shared_ptr<const animation::CharacterClips> TestClips() {
  assets::Skeleton skeleton{.joints = {assets::JointData{.name = "Root"}}, .rest_transforms = {math::Mat4(1.0F)}};
  return Rig(skeleton);
}

// The Walker: a Character whose Clips move its Joints. Its Skeleton is a Pelvis
// above its Root, a Head above the Pelvis, two legs hanging from it and a left
// arm, as Rig hangs one, from a shoulder to its left; its Clips hold the Pelvis
// at its stance's height and swing the legs about the Pelvis's X axis as it
// walks, so a test can say where a Shot finds its head or a leg. Its weapon
// socket is on its Pelvis, the Grip kRigGripAhead ahead of its right hip.
inline constexpr float kPelvisStanding = 1.0F;
inline constexpr float kPelvisCrouching = 0.6F;
inline constexpr float kPelvisProne = 0.25F;
// Where the Head Joint is above the Pelvis, and each hip to the Pelvis's side;
// the Walker's left is -X, as it faces -Z.
inline constexpr float kHeadAbovePelvis = 0.6F;
inline constexpr float kHipSide = 0.15F;
// Where the Walker's left shoulder is above its Pelvis.
inline constexpr float kWalkerShoulderAbovePelvis = 0.45F;
// How far a leg swings ahead (+) or behind (-) at a Foot contact, in radians:
// the landing foot's leg ahead, the other behind. 30 degrees.
inline constexpr float kLegSwing = 0.5235988F;

// The Walker's Joints, in its assets::Skeleton's order: parents before
// children, its legs before its Head, where ozz-animation keeps them (depth
// first) after it, then its left arm.
inline constexpr std::uint32_t kWalkerRoot = 0;
inline constexpr std::uint32_t kWalkerPelvis = 1;
inline constexpr std::uint32_t kWalkerLeftLeg = 2;
inline constexpr std::uint32_t kWalkerRightLeg = 3;
inline constexpr std::uint32_t kWalkerHead = 4;
inline constexpr std::uint32_t kWalkerLeftArm = 5;
inline constexpr std::uint32_t kWalkerLeftForearm = 6;
inline constexpr std::uint32_t kWalkerLeftHand = 7;

// Where the Walker's left shoulder is from its Pelvis, and each bone of the arm
// from the Joint above it.
inline math::Vec3 WalkerShoulder() { return {-kRigShoulderSide, kWalkerShoulderAbovePelvis, 0.0F}; }
inline math::Vec3 WalkerBone() { return {0.0F, -kRigBone, 0.0F}; }

// The Walker's Skeleton, standing in its Rest pose.
inline assets::Skeleton WalkerSkeleton() {
  const math::Quat none(1.0F, 0.0F, 0.0F, 0.0F);
  const math::Vec3 unit(1.0F);
  const math::Mat4 pelvis = math::ToMat4(math::Vec3(0.0F, kPelvisStanding, 0.0F), none, unit);
  const math::Mat4 shoulder = pelvis * math::ToMat4(WalkerShoulder(), none, unit);
  const math::Mat4 elbow = shoulder * math::ToMat4(WalkerBone(), none, unit);
  return {
      .joints = {{.name = "Root"},
                 {.name = "Root/Pelvis", .parent = kWalkerRoot, .translation = {0.0F, kPelvisStanding, 0.0F}},
                 {.name = "Root/Pelvis/LeftLeg", .parent = kWalkerPelvis, .translation = {-kHipSide, 0.0F, 0.0F}},
                 {.name = "Root/Pelvis/RightLeg", .parent = kWalkerPelvis, .translation = {kHipSide, 0.0F, 0.0F}},
                 {.name = "Root/Pelvis/Head", .parent = kWalkerPelvis, .translation = {0.0F, kHeadAbovePelvis, 0.0F}},
                 {.name = "Root/Pelvis/LeftArm", .parent = kWalkerPelvis, .translation = WalkerShoulder()},
                 {.name = "Root/Pelvis/LeftArm/LeftForearm", .parent = kWalkerLeftArm, .translation = WalkerBone()},
                 {.name = "Root/Pelvis/LeftArm/LeftForearm/LeftHand",
                  .parent = kWalkerLeftForearm,
                  .translation = WalkerBone()}},
      .rest_transforms = {math::Mat4(1.0F), pelvis,
                          pelvis * math::ToMat4(math::Vec3(-kHipSide, 0.0F, 0.0F), none, unit),
                          pelvis * math::ToMat4(math::Vec3(kHipSide, 0.0F, 0.0F), none, unit),
                          pelvis * math::ToMat4(math::Vec3(0.0F, kHeadAbovePelvis, 0.0F), none, unit), shoulder, elbow,
                          elbow * math::ToMat4(WalkerBone(), none, unit)}};
}

// The tracks of a Walker Clip, its Pelvis pelvis high, in ozz-animation's order
// (Root, Pelvis, Head, LeftLeg, RightLeg, LeftArm, LeftForearm, LeftHand), each
// Joint where the Rest pose has it from its parent; its legs straight down.
inline decltype(ozz::animation::offline::RawAnimation::tracks) WalkerTracks(float pelvis) {
  using ozz::math::Float3;
  const math::Vec3 shoulder = WalkerShoulder();
  const std::array<Float3, 7> at{
      Float3(0.0F, pelvis, 0.0F),   Float3(0.0F, kHeadAbovePelvis, 0.0F),       Float3(-kHipSide, 0.0F, 0.0F),
      Float3(kHipSide, 0.0F, 0.0F), Float3(shoulder.x, shoulder.y, shoulder.z), Float3(0.0F, -kRigBone, 0.0F),
      Float3(0.0F, -kRigBone, 0.0F)};
  decltype(ozz::animation::offline::RawAnimation::tracks) tracks(at.size() + 1);
  for (std::size_t joint = 0; joint < at.size(); ++joint) {
    tracks.at(joint + 1).translations.push_back({.time = 0.0F, .value = at.at(joint)});
  }
  return tracks;
}

// The left leg's swing at ratio of a walk whose left foot lands at left and
// whose right lands at right, both ratios of its duration: ahead by kLegSwing
// at its own contact, behind by it at the other's, linearly between.
inline float LeftLegSwing(float ratio, float left, float right) {
  // How far the cycle is from the left contact, and the right contact is.
  const float from_left = std::fmod(ratio - left + 1.0F, 1.0F);
  const float to_right = std::fmod(right - left + 1.0F, 1.0F);
  if (from_left <= to_right) {
    return kLegSwing - (2.0F * kLegSwing * from_left / to_right);
  }
  return -kLegSwing + (2.0F * kLegSwing * (from_left - to_right) / (1.0F - to_right));
}

// A Walker Clip duration seconds long, its Pelvis pelvis high. A walk, with its
// stride_length, lands its left foot at left and its right at right, ratios of
// its duration, and swings its legs between; any other Clip holds them
// straight down.
inline animation::Clip WalkerClip(float pelvis, float duration, float stride_length = 0.0F, float left = 0.0F,
                                  float right = 0.5F) {
  using ozz::math::Float3;
  using ozz::math::Quaternion;
  ozz::animation::offline::RawAnimation raw;
  raw.duration = duration;
  raw.tracks = WalkerTracks(pelvis);
  const bool walks = stride_length > 0.0F;
  std::vector<float> ratios{0.0F, 1.0F};
  if (walks) {
    ratios = {0.0F, left, right, 1.0F};
    std::ranges::sort(ratios);
    ratios.erase(std::ranges::unique(ratios).begin(), ratios.end());
  }
  for (const float ratio : ratios) {
    const float swing = walks ? LeftLegSwing(ratio, left, right) : 0.0F;
    // A turn about +X swings a leg hanging down -Y ahead, to -Z.
    raw.tracks[3].rotations.push_back(
        {.time = ratio * duration, .value = Quaternion::FromAxisAngle(Float3(1.0F, 0.0F, 0.0F), swing)});
    raw.tracks[4].rotations.push_back(
        {.time = ratio * duration, .value = Quaternion::FromAxisAngle(Float3(1.0F, 0.0F, 0.0F), -swing)});
  }
  auto built = ozz::animation::offline::AnimationBuilder()(raw);
  EXPECT_TRUE(built);
  animation::Clip clip{.animation = std::move(*built), .stride_length = stride_length, .foot_contacts = {}};
  if (walks) {
    clip.foot_contacts = {{.foot = assets::Foot::kLeft, .ratio = left}, {.foot = assets::Foot::kRight, .ratio = right}};
    if (right < left) {
      std::swap(clip.foot_contacts[0], clip.foot_contacts[1]);
    }
  }
  return clip;
}

// A Walker stance's Clips, its Pelvis pelvis high: one-second walks of these
// stride lengths, each landing its left foot at its start and its right
// halfway, and still poses for the rest.
inline animation::StanceClips WalkerStanceOf(float pelvis, float forward, float backward, float left, float right,
                                             bool sides = true) {
  animation::StanceClips clips;
  clips.idle = WalkerClip(pelvis, 1.0F);
  clips.forward = WalkerClip(pelvis, 1.0F, forward);
  clips.backward = WalkerClip(pelvis, 1.0F, backward);
  clips.aim_down = WalkerClip(pelvis, 1.0F);
  clips.aim_center = WalkerClip(pelvis, 1.0F);
  clips.aim_up = WalkerClip(pelvis, 1.0F);
  clips.ads = WalkerClip(pelvis, 1.0F);
  if (sides) {
    clips.left = WalkerClip(pelvis, 1.0F, left);
    clips.right = WalkerClip(pelvis, 1.0F, right);
  }
  return clips;
}

// How far the Walker's air Clip swings a leg, in radians: 90 degrees, so the
// leg lies level with its hip.
inline constexpr float kAirTuck = 1.5707964F;

// The Walker's air Clip, its Pelvis standing high: its left leg tucked kAirTuck
// ahead throughout, its right leg swinging from kAirTuck ahead at its start
// (rising) through straight down halfway (the top of a jump) to kAirTuck behind
// at its end (falling). Neither is where a Clip on the ground puts a leg.
inline animation::Clip WalkerAirClip() {
  using ozz::math::Float3;
  using ozz::math::Quaternion;
  ozz::animation::offline::RawAnimation raw;
  raw.duration = 1.0F;
  raw.tracks = WalkerTracks(kPelvisStanding);
  const Float3 across(1.0F, 0.0F, 0.0F);
  raw.tracks[3].rotations.push_back({.time = 0.0F, .value = Quaternion::FromAxisAngle(across, kAirTuck)});
  raw.tracks[4].rotations.push_back({.time = 0.0F, .value = Quaternion::FromAxisAngle(across, kAirTuck)});
  raw.tracks[4].rotations.push_back({.time = 0.5F, .value = Quaternion::identity()});
  raw.tracks[4].rotations.push_back({.time = 1.0F, .value = Quaternion::FromAxisAngle(across, -kAirTuck)});
  auto built = ozz::animation::offline::AnimationBuilder()(raw);
  EXPECT_TRUE(built);
  return animation::Clip{.animation = std::move(*built), .stride_length = 0.0F, .foot_contacts = {}};
}

// Where standing's right walk lands each foot: a quarter of a cycle later than
// every other walk does, so blending it with one unsynchronised would cross
// their legs.
inline constexpr float kRightWalkLeft = 0.25F;
inline constexpr float kRightWalkRight = 0.75F;

// The Walker's Clips, with TestClips' stride lengths.
inline std::shared_ptr<const animation::CharacterClips> WalkerClips() {
  ozz::animation::offline::RawSkeleton raw;
  raw.roots.resize(1);
  auto& root = raw.roots[0];
  root.name = "Root";
  root.transform = ozz::math::Transform::identity();
  root.children.resize(1);
  auto& pelvis = root.children[0];
  pelvis.name = "Root/Pelvis";
  pelvis.transform = ozz::math::Transform::identity();
  pelvis.transform.translation = ozz::math::Float3(0.0F, kPelvisStanding, 0.0F);
  const math::Vec3 shoulder = WalkerShoulder();
  const std::array<std::pair<const char*, ozz::math::Float3>, 4> children{{
      {"Root/Pelvis/Head", ozz::math::Float3(0.0F, kHeadAbovePelvis, 0.0F)},
      {"Root/Pelvis/LeftLeg", ozz::math::Float3(-kHipSide, 0.0F, 0.0F)},
      {"Root/Pelvis/RightLeg", ozz::math::Float3(kHipSide, 0.0F, 0.0F)},
      {"Root/Pelvis/LeftArm", ozz::math::Float3(shoulder.x, shoulder.y, shoulder.z)},
  }};
  pelvis.children.resize(children.size());
  for (std::size_t child = 0; child < children.size(); ++child) {
    pelvis.children[child].name = children.at(child).first;
    pelvis.children[child].transform = ozz::math::Transform::identity();
    pelvis.children[child].transform.translation = children.at(child).second;
  }
  ozz::animation::offline::RawSkeleton::Joint* bone = &pelvis.children.back();
  for (const char* const name : {"Root/Pelvis/LeftArm/LeftForearm", "Root/Pelvis/LeftArm/LeftForearm/LeftHand"}) {
    bone->children.resize(1);
    bone = &bone->children.front();
    bone->name = name;
    bone->transform = ozz::math::Transform::identity();
    bone->transform.translation = ozz::math::Float3(0.0F, -kRigBone, 0.0F);
  }
  auto skeleton = ozz::animation::offline::SkeletonBuilder()(raw);
  EXPECT_TRUE(skeleton);

  auto clips = std::make_shared<animation::CharacterClips>();
  clips->skeleton = std::move(*skeleton);
  clips->joints = {kWalkerRoot,     kWalkerPelvis,  kWalkerHead,        kWalkerLeftLeg,
                   kWalkerRightLeg, kWalkerLeftArm, kWalkerLeftForearm, kWalkerLeftHand};
  animation::StanceClips standing =
      WalkerStanceOf(kPelvisStanding, kStandingForward, kStandingBackward, kStandingLeft, kStandingRight);
  standing.right = WalkerClip(kPelvisStanding, 1.0F, kStandingRight, kRightWalkLeft, kRightWalkRight);
  standing.sprint = WalkerClip(kPelvisStanding, 1.0F, kStandingSprint);
  clips->stances[static_cast<std::size_t>(physics::Stance::kStanding)] = std::move(standing);
  clips->stances[static_cast<std::size_t>(physics::Stance::kCrouching)] =
      WalkerStanceOf(kPelvisCrouching, kCrouchingForward, 1.2F, 1.3F, 1.3F);
  clips->stances[static_cast<std::size_t>(physics::Stance::kProne)] =
      WalkerStanceOf(kPelvisProne, kProneForward, kProneBackward, 0.0F, 0.0F, false);
  clips->air = WalkerAirClip();
  clips->transitions = {.standing_crouching = 0.25F, .standing_prone = 0.8F, .crouching_prone = 0.6F};
  clips->weapon_socket =
      animation::WeaponSocket{.right_hand = kWalkerPelvis,
                              .frame = math::ToMat4(math::Vec3(kHipSide, 0.0F, -kRigGripAhead),
                                                    math::Quat(1.0F, 0.0F, 0.0F, 0.0F), math::Vec3(1.0F)),
                              .left_hand = kWalkerLeftHand};
  return clips;
}

// The Holder: a Character whose Aim offset bends it and whose ADS grip raises
// its right arm while it holds the rifle (ADR-0066). Its Skeleton is a Pelvis
// above its Root, a Head above the Pelvis, and an arm hanging from a shoulder
// to each side of the Pelvis and above it: its RightArm, a RightHand kHolderArm
// down it with the weapon socket on it, and its LeftArm, a LeftForearm
// kHolderUpperArm down it, bent ahead at the elbow by kHolderElbowBend, and a
// LeftHand kHolderForearm down that. Its aim_down bends the Pelvis forward, its
// aim_up back, and its ads swings the right arm up ahead of it and shoulders
// the rifle, standing, with its Sight on the Eye (HolderAdsClip); every other
// Clip holds it upright, its right arm down, and every one holds the left arm
// as it is, so where the left hand ends up is the IK's. Each stance bends and
// raises it by its own angles, so a test can tell whose it took.
inline constexpr float kHolderShoulderSide = 0.25F;
inline constexpr float kHolderShoulderAbovePelvis = 0.45F;
// How far each stance's aim_down bends the Pelvis forward and its aim_up back,
// and how far its ads swings the right arm up ahead from hanging down, in
// radians.
inline constexpr float kStandingAimBend = 0.7853982F;    // 45 degrees
inline constexpr float kCrouchingAimBend = 1.3089969F;   // 75 degrees
inline constexpr float kProneAimBend = 0.2617994F;       // 15 degrees
inline constexpr float kStandingAdsRaise = 1.5707964F;   // 90 degrees
inline constexpr float kCrouchingAdsRaise = 1.0471976F;  // 60 degrees
inline constexpr float kProneAdsRaise = 0.5235988F;      // 30 degrees
inline constexpr float kHolderArm = 0.6F;
// Where the Holder sees from, standing, from its feet: its Character's Eye.
inline constexpr math::Vec3 kHolderEye{0.0F, 1.7F, 0.0F};
inline constexpr float kHolderUpperArm = 0.5F;
inline constexpr float kHolderForearm = 0.5F;
inline constexpr float kHolderElbowBend = 0.5235988F;  // 30 degrees

// The Holder's Joints, in its assets::Skeleton's order, which is ozz-animation's
// too.
inline constexpr std::uint32_t kHolderRoot = 0;
inline constexpr std::uint32_t kHolderPelvis = 1;
inline constexpr std::uint32_t kHolderHead = 2;
inline constexpr std::uint32_t kHolderRightArm = 3;
inline constexpr std::uint32_t kHolderRightHand = 4;
inline constexpr std::uint32_t kHolderLeftArm = 5;
inline constexpr std::uint32_t kHolderLeftForearm = 6;
inline constexpr std::uint32_t kHolderLeftHand = 7;

// Each Holder Joint's name, parent and translation from it, in order.
struct HolderJoint {
  const char* name;
  std::uint32_t parent;
  math::Vec3 translation;
};
inline std::array<HolderJoint, 8> HolderJoints() {
  return {{
      {"Root", assets::kJointNoParent, math::Vec3(0.0F)},
      {"Root/Pelvis", kHolderRoot, math::Vec3(0.0F, kPelvisStanding, 0.0F)},
      {"Root/Pelvis/Head", kHolderPelvis, math::Vec3(0.0F, kHeadAbovePelvis, 0.0F)},
      {"Root/Pelvis/RightArm", kHolderPelvis, math::Vec3(kHolderShoulderSide, kHolderShoulderAbovePelvis, 0.0F)},
      {"Root/Pelvis/RightArm/RightHand", kHolderRightArm, math::Vec3(0.0F, -kHolderArm, 0.0F)},
      {"Root/Pelvis/LeftArm", kHolderPelvis, math::Vec3(-kHolderShoulderSide, kHolderShoulderAbovePelvis, 0.0F)},
      {"Root/Pelvis/LeftArm/LeftForearm", kHolderLeftArm, math::Vec3(0.0F, -kHolderUpperArm, 0.0F)},
      {"Root/Pelvis/LeftArm/LeftForearm/LeftHand", kHolderLeftForearm, math::Vec3(0.0F, -kHolderForearm, 0.0F)},
  }};
}

// The Holder's Skeleton, standing upright in its Rest pose, its arms down.
inline assets::Skeleton HolderSkeleton() {
  assets::Skeleton skeleton;
  for (const HolderJoint& joint : HolderJoints()) {
    const math::Mat4 local = math::ToMat4(joint.translation, math::Quat(1.0F, 0.0F, 0.0F, 0.0F), math::Vec3(1.0F));
    skeleton.joints.push_back({.name = joint.name, .parent = joint.parent, .translation = joint.translation});
    skeleton.rest_transforms.push_back(
        joint.parent == assets::kJointNoParent ? local : skeleton.rest_transforms[joint.parent] * local);
  }
  return skeleton;
}

// A Holder Clip's raw animation, one second long: its Pelvis pelvis high and
// bent ahead by bend, its right arm swung up ahead by raise.
inline ozz::animation::offline::RawAnimation HolderRaw(float pelvis, float bend, float raise) {
  using ozz::math::Float3;
  using ozz::math::Quaternion;
  const std::array<HolderJoint, 8> joints = HolderJoints();
  ozz::animation::offline::RawAnimation raw;
  raw.duration = 1.0F;
  raw.tracks.resize(joints.size());
  for (std::size_t joint = 0; joint < joints.size(); ++joint) {
    const math::Vec3& at = joints.at(joint).translation;
    raw.tracks[joint].translations.push_back({.time = 0.0F, .value = Float3(at.x, at.y, at.z)});
  }
  raw.tracks[kHolderPelvis].translations[0].value = Float3(0.0F, pelvis, 0.0F);
  // A turn about +X tips +Y back, to +Z, and swings -Y ahead, to -Z.
  raw.tracks[kHolderPelvis].rotations.push_back(
      {.time = 0.0F, .value = Quaternion::FromAxisAngle(Float3(1.0F, 0.0F, 0.0F), -bend)});
  raw.tracks[kHolderRightArm].rotations.push_back(
      {.time = 0.0F, .value = Quaternion::FromAxisAngle(Float3(1.0F, 0.0F, 0.0F), raise)});
  raw.tracks[kHolderLeftForearm].rotations.push_back(
      {.time = 0.0F, .value = Quaternion::FromAxisAngle(Float3(1.0F, 0.0F, 0.0F), kHolderElbowBend)});
  return raw;
}

// raw built into a Clip; with a stride_length, a still walk of it.
inline animation::Clip HolderBuilt(const ozz::animation::offline::RawAnimation& raw, float stride_length) {
  auto built = ozz::animation::offline::AnimationBuilder()(raw);
  EXPECT_TRUE(built);
  return animation::Clip{
      .animation = std::move(*built), .stride_length = stride_length, .foot_contacts = StepsOf(stride_length)};
}

// A Holder Clip one second long, as HolderRaw's; with a stride_length, a still
// walk of it.
inline animation::Clip HolderClip(float pelvis, float bend = 0.0F, float raise = 0.0F, float stride_length = 0.0F) {
  return HolderBuilt(HolderRaw(pelvis, bend, raise), stride_length);
}

// A Holder's ADS grip, its Pelvis pelvis high: its right arm swung up ahead by
// raise, and its hand turned back against the raise, so the rifle on it points
// ahead, and moved along the arm to where, standing, the Eye is: the Grip, and
// the Sight on it, sit on the Eye, the barrel along a level view. In a lower
// stance it is as high above the Pelvis as standing's.
inline animation::Clip HolderAdsClip(float pelvis, float raise) {
  using ozz::math::Float3;
  using ozz::math::Quaternion;
  ozz::animation::offline::RawAnimation raw = HolderRaw(pelvis, 0.0F, raise);
  const math::Vec3 shoulder(kHolderShoulderSide, pelvis + kHolderShoulderAbovePelvis, 0.0F);
  const math::Vec3 eye(kHolderEye.x, pelvis + (kHolderEye.y - kPelvisStanding), kHolderEye.z);
  const math::Vec3 along = glm::angleAxis(-raise, math::Vec3(1.0F, 0.0F, 0.0F)) * (eye - shoulder);
  raw.tracks[kHolderRightHand].translations[0].value = Float3(along.x, along.y, along.z);
  raw.tracks[kHolderRightHand].rotations.push_back(
      {.time = 0.0F, .value = Quaternion::FromAxisAngle(Float3(1.0F, 0.0F, 0.0F), -raise)});
  return HolderBuilt(raw, 0.0F);
}

// A Holder stance's Clips, its Pelvis pelvis high: its Aim offset bends it by
// bend, its ADS grip raises its right arm by raise, and its walks, of these
// stride lengths, hold it upright.
inline animation::StanceClips HolderStanceOf(float pelvis, float bend, float raise, float forward, float backward,
                                             float left, float right, bool sides = true) {
  animation::StanceClips clips;
  clips.idle = HolderClip(pelvis);
  clips.forward = HolderClip(pelvis, 0.0F, 0.0F, forward);
  clips.backward = HolderClip(pelvis, 0.0F, 0.0F, backward);
  clips.aim_down = HolderClip(pelvis, bend);
  clips.aim_center = HolderClip(pelvis);
  clips.aim_up = HolderClip(pelvis, -bend);
  clips.ads = HolderAdsClip(pelvis, raise);
  if (sides) {
    clips.left = HolderClip(pelvis, 0.0F, 0.0F, left);
    clips.right = HolderClip(pelvis, 0.0F, 0.0F, right);
  }
  return clips;
}

// The ozz-animation RawSkeleton Joint of the Holder's joint, its children under
// it.
inline ozz::animation::offline::RawSkeleton::Joint HolderRawJoint(std::uint32_t joint) {
  const std::array<HolderJoint, 8> joints = HolderJoints();
  ozz::animation::offline::RawSkeleton::Joint raw;
  raw.name = joints.at(joint).name;
  raw.transform = ozz::math::Transform::identity();
  const math::Vec3& at = joints.at(joint).translation;
  raw.transform.translation = ozz::math::Float3(at.x, at.y, at.z);
  for (std::uint32_t child = 0; child < joints.size(); ++child) {
    if (joints.at(child).parent == joint) {
      raw.children.push_back(HolderRawJoint(child));
    }
  }
  return raw;
}

// The Holder's Clips, with RestClips' stride lengths, and its weapon socket: on
// its RightHand, unturned, its left hand its LeftHand.
inline std::shared_ptr<const animation::CharacterClips> HolderClips() {
  ozz::animation::offline::RawSkeleton raw;
  raw.roots.push_back(HolderRawJoint(kHolderRoot));
  auto skeleton = ozz::animation::offline::SkeletonBuilder()(raw);
  EXPECT_TRUE(skeleton);

  auto clips = std::make_shared<animation::CharacterClips>();
  clips->skeleton = std::move(*skeleton);
  for (std::uint32_t joint = 0; joint < HolderJoints().size(); ++joint) {
    clips->joints.push_back(joint);
  }
  animation::StanceClips standing = HolderStanceOf(kPelvisStanding, kStandingAimBend, kStandingAdsRaise,
                                                   kStandingForward, kStandingBackward, kStandingLeft, kStandingRight);
  standing.sprint = HolderClip(kPelvisStanding, 0.0F, 0.0F, kStandingSprint);
  clips->stances[static_cast<std::size_t>(physics::Stance::kStanding)] = std::move(standing);
  clips->stances[static_cast<std::size_t>(physics::Stance::kCrouching)] =
      HolderStanceOf(kPelvisCrouching, kCrouchingAimBend, kCrouchingAdsRaise, kCrouchingForward, 1.2F, 1.3F, 1.3F);
  clips->stances[static_cast<std::size_t>(physics::Stance::kProne)] =
      HolderStanceOf(kPelvisProne, kProneAimBend, kProneAdsRaise, kProneForward, kProneBackward, 0.0F, 0.0F, false);
  clips->air = HolderClip(kPelvisStanding);
  clips->transitions = {.standing_crouching = 0.25F, .standing_prone = 0.8F, .crouching_prone = 0.6F};
  clips->weapon_socket =
      animation::WeaponSocket{.right_hand = kHolderRightHand, .frame = math::Mat4(1.0F), .left_hand = kHolderLeftHand};
  return clips;
}

// The Holder's rifle: its Grip at its origin, its Fore-end kHolderForeEnd ahead
// of it and its Muzzle kHolderMuzzle ahead, each unturned, along -Z.
inline constexpr float kHolderForeEnd = 0.4F;
inline constexpr float kHolderMuzzle = 0.7F;
inline weapon::Markers HolderRifle() {
  return weapon::Markers{
      .grip = math::Mat4(1.0F),
      .muzzle =
          math::ToMat4(math::Vec3(0.0F, 0.0F, -kHolderMuzzle), math::Quat(1.0F, 0.0F, 0.0F, 0.0F), math::Vec3(1.0F)),
      .sight = math::Mat4(1.0F),
      .fore_end =
          math::ToMat4(math::Vec3(0.0F, 0.0F, -kHolderForeEnd), math::Quat(1.0F, 0.0F, 0.0F, 0.0F), math::Vec3(1.0F)),
  };
}

}  // namespace augusta::clips_fixture

#endif  // AUGUSTA_TESTS_CLIPS_FIXTURE_H_
