#ifndef AUGUSTA_TESTS_CLIPS_FIXTURE_H_
#define AUGUSTA_TESTS_CLIPS_FIXTURE_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
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
// Character's art: TestClips, a Skeleton of its root Joint alone and still
// Clips, each locomotion Clip with the stride length and duration the test gives
// it; the Walker, whose Clips move its Joints, for a test of a pose; the
// Aimer, whose Aim offset and ADS grip bend and raise it; and the Holder, the
// Aimer holding the rifle.
namespace augusta::clips_fixture {

// A still Clip duration seconds long, one track for the root Joint, which a
// cooker would have measured stride_length meters of stride from.
inline animation::Clip StillClip(float duration, float stride_length = 0.0F) {
  ozz::animation::offline::RawAnimation raw;
  raw.duration = duration;
  raw.tracks.resize(1);
  auto built = ozz::animation::offline::AnimationBuilder()(raw);
  EXPECT_TRUE(built);
  animation::Clip clip{.animation = std::move(*built), .stride_length = stride_length, .foot_contacts = {}};
  if (stride_length > 0.0F) {
    clip.foot_contacts = {{.foot = assets::Foot::kLeft, .ratio = 0.0F}, {.foot = assets::Foot::kRight, .ratio = 0.5F}};
  }
  return clip;
}

// A locomotion Clip one second long that covers stride_length meters a cycle:
// its speed is stride_length meters a second.
inline animation::Clip Locomotion(float stride_length) { return StillClip(1.0F, stride_length); }

// A stance's Clips with these locomotion stride lengths, one second each, and
// still poses for the rest; no diagonals, no sprint, no sides when sides is false.
inline animation::StanceClips StanceOf(float forward, float backward, float left, float right, bool sides = true) {
  animation::StanceClips clips;
  clips.idle = StillClip(1.0F);
  clips.forward = Locomotion(forward);
  clips.backward = Locomotion(backward);
  clips.aim_down = StillClip(1.0F);
  clips.aim_center = StillClip(1.0F);
  clips.aim_up = StillClip(1.0F);
  clips.ads = StillClip(1.0F);
  if (sides) {
    clips.left = Locomotion(left);
    clips.right = Locomotion(right);
  }
  return clips;
}

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

// A Character's Clips: each stance's, with the strides above, standing's sprint,
// prone with no sides, and its air Clip.
inline std::shared_ptr<const animation::CharacterClips> TestClips() {
  ozz::animation::offline::RawSkeleton raw;
  raw.roots.resize(1);
  raw.roots[0].name = "Root";
  raw.roots[0].transform = ozz::math::Transform::identity();
  auto skeleton = ozz::animation::offline::SkeletonBuilder()(raw);
  EXPECT_TRUE(skeleton);

  auto clips = std::make_shared<animation::CharacterClips>();
  clips->skeleton = std::move(*skeleton);
  clips->joints = {0};
  animation::StanceClips standing = StanceOf(kStandingForward, kStandingBackward, kStandingLeft, kStandingRight);
  standing.sprint = Locomotion(kStandingSprint);
  clips->stances[static_cast<std::size_t>(physics::Stance::kStanding)] = std::move(standing);
  clips->stances[static_cast<std::size_t>(physics::Stance::kCrouching)] = StanceOf(kCrouchingForward, 1.2F, 1.3F, 1.3F);
  clips->stances[static_cast<std::size_t>(physics::Stance::kProne)] =
      StanceOf(kProneForward, kProneBackward, 0.0F, 0.0F, false);
  clips->air = StillClip(1.0F);
  clips->transitions = {.standing_crouching = 0.25F, .standing_prone = 0.8F, .crouching_prone = 0.6F};
  return clips;
}

// The Walker: a Character whose Clips move its Joints. Its Skeleton is a Pelvis
// above its Root, a Head above the Pelvis and two legs hanging from it; its
// Clips hold the Pelvis at its stance's height and swing the legs about the
// Pelvis's X axis as it walks, so a test can say where a Shot finds its head or
// a leg.
inline constexpr float kPelvisStanding = 1.0F;
inline constexpr float kPelvisCrouching = 0.6F;
inline constexpr float kPelvisProne = 0.25F;
// Where the Head Joint is above the Pelvis, and each hip to the Pelvis's side;
// the Walker's left is -X, as it faces -Z.
inline constexpr float kHeadAbovePelvis = 0.6F;
inline constexpr float kHipSide = 0.15F;
// How far a leg swings ahead (+) or behind (-) at a Foot contact, in radians:
// the landing foot's leg ahead, the other behind. 30 degrees.
inline constexpr float kLegSwing = 0.5235988F;

// The Walker's Joints, in its assets::Skeleton's order: parents before
// children, its legs before its Head, where ozz-animation keeps them (depth
// first) after it.
inline constexpr std::uint32_t kWalkerRoot = 0;
inline constexpr std::uint32_t kWalkerPelvis = 1;
inline constexpr std::uint32_t kWalkerLeftLeg = 2;
inline constexpr std::uint32_t kWalkerRightLeg = 3;
inline constexpr std::uint32_t kWalkerHead = 4;

// The Walker's Skeleton, standing in its Rest pose.
inline assets::Skeleton WalkerSkeleton() {
  const math::Quat none(1.0F, 0.0F, 0.0F, 0.0F);
  const math::Vec3 unit(1.0F);
  const math::Mat4 pelvis = math::ToMat4(math::Vec3(0.0F, kPelvisStanding, 0.0F), none, unit);
  return {
      .joints = {{.name = "Root"},
                 {.name = "Root/Pelvis", .parent = kWalkerRoot, .translation = {0.0F, kPelvisStanding, 0.0F}},
                 {.name = "Root/Pelvis/LeftLeg", .parent = kWalkerPelvis, .translation = {-kHipSide, 0.0F, 0.0F}},
                 {.name = "Root/Pelvis/RightLeg", .parent = kWalkerPelvis, .translation = {kHipSide, 0.0F, 0.0F}},
                 {.name = "Root/Pelvis/Head", .parent = kWalkerPelvis, .translation = {0.0F, kHeadAbovePelvis, 0.0F}}},
      .rest_transforms = {math::Mat4(1.0F), pelvis,
                          pelvis * math::ToMat4(math::Vec3(-kHipSide, 0.0F, 0.0F), none, unit),
                          pelvis * math::ToMat4(math::Vec3(kHipSide, 0.0F, 0.0F), none, unit),
                          pelvis * math::ToMat4(math::Vec3(0.0F, kHeadAbovePelvis, 0.0F), none, unit)}};
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
  // ozz-animation's order: Root, Pelvis, Head, LeftLeg, RightLeg.
  raw.tracks.resize(5);
  raw.tracks[1].translations.push_back({.time = 0.0F, .value = Float3(0.0F, pelvis, 0.0F)});
  raw.tracks[2].translations.push_back({.time = 0.0F, .value = Float3(0.0F, kHeadAbovePelvis, 0.0F)});
  raw.tracks[3].translations.push_back({.time = 0.0F, .value = Float3(-kHipSide, 0.0F, 0.0F)});
  raw.tracks[4].translations.push_back({.time = 0.0F, .value = Float3(kHipSide, 0.0F, 0.0F)});
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
  // ozz-animation's order: Root, Pelvis, Head, LeftLeg, RightLeg.
  raw.tracks.resize(5);
  raw.tracks[1].translations.push_back({.time = 0.0F, .value = Float3(0.0F, kPelvisStanding, 0.0F)});
  raw.tracks[2].translations.push_back({.time = 0.0F, .value = Float3(0.0F, kHeadAbovePelvis, 0.0F)});
  raw.tracks[3].translations.push_back({.time = 0.0F, .value = Float3(-kHipSide, 0.0F, 0.0F)});
  raw.tracks[4].translations.push_back({.time = 0.0F, .value = Float3(kHipSide, 0.0F, 0.0F)});
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
  const std::array<std::pair<const char*, ozz::math::Float3>, 3> children{{
      {"Root/Pelvis/Head", ozz::math::Float3(0.0F, kHeadAbovePelvis, 0.0F)},
      {"Root/Pelvis/LeftLeg", ozz::math::Float3(-kHipSide, 0.0F, 0.0F)},
      {"Root/Pelvis/RightLeg", ozz::math::Float3(kHipSide, 0.0F, 0.0F)},
  }};
  pelvis.children.resize(children.size());
  for (std::size_t child = 0; child < children.size(); ++child) {
    pelvis.children[child].name = children.at(child).first;
    pelvis.children[child].transform = ozz::math::Transform::identity();
    pelvis.children[child].transform.translation = children.at(child).second;
  }
  auto skeleton = ozz::animation::offline::SkeletonBuilder()(raw);
  EXPECT_TRUE(skeleton);

  auto clips = std::make_shared<animation::CharacterClips>();
  clips->skeleton = std::move(*skeleton);
  clips->joints = {kWalkerRoot, kWalkerPelvis, kWalkerHead, kWalkerLeftLeg, kWalkerRightLeg};
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
  return clips;
}

// The Aimer: a Character whose Aim offset bends it and whose ADS grip raises
// its arm. Its Skeleton is a Pelvis above its Root, a Head above the Pelvis and
// an Arm hanging from its shoulder, to the Pelvis's right and above it. Its
// aim_down bends the Pelvis forward, its aim_up back, and its ads swings the Arm
// up ahead of it; every other Clip holds it upright, its Arm down. Each stance
// bends and raises it by its own angles, so a test can tell whose it took.
inline constexpr float kAimerShoulderSide = 0.25F;
inline constexpr float kAimerShoulderAbovePelvis = 0.45F;
// How far each stance's aim_down bends the Pelvis forward and its aim_up back,
// and how far its ads swings the Arm up ahead from hanging down, in radians.
inline constexpr float kStandingAimBend = 0.7853982F;    // 45 degrees
inline constexpr float kCrouchingAimBend = 1.3089969F;   // 75 degrees
inline constexpr float kProneAimBend = 0.2617994F;       // 15 degrees
inline constexpr float kStandingAdsRaise = 1.5707964F;   // 90 degrees
inline constexpr float kCrouchingAdsRaise = 1.0471976F;  // 60 degrees
inline constexpr float kProneAdsRaise = 0.5235988F;      // 30 degrees

// The Aimer's Joints, in its assets::Skeleton's order, which is ozz-animation's
// too.
inline constexpr std::uint32_t kAimerRoot = 0;
inline constexpr std::uint32_t kAimerPelvis = 1;
inline constexpr std::uint32_t kAimerHead = 2;
inline constexpr std::uint32_t kAimerArm = 3;

// The Aimer's Skeleton, standing upright in its Rest pose, its Arm down.
inline assets::Skeleton AimerSkeleton() {
  const math::Quat none(1.0F, 0.0F, 0.0F, 0.0F);
  const math::Vec3 unit(1.0F);
  const math::Vec3 shoulder(kAimerShoulderSide, kAimerShoulderAbovePelvis, 0.0F);
  const math::Mat4 pelvis = math::ToMat4(math::Vec3(0.0F, kPelvisStanding, 0.0F), none, unit);
  return {
      .joints = {{.name = "Root"},
                 {.name = "Root/Pelvis", .parent = kAimerRoot, .translation = {0.0F, kPelvisStanding, 0.0F}},
                 {.name = "Root/Pelvis/Head", .parent = kAimerPelvis, .translation = {0.0F, kHeadAbovePelvis, 0.0F}},
                 {.name = "Root/Pelvis/Arm", .parent = kAimerPelvis, .translation = shoulder}},
      .rest_transforms = {math::Mat4(1.0F), pelvis,
                          pelvis * math::ToMat4(math::Vec3(0.0F, kHeadAbovePelvis, 0.0F), none, unit),
                          pelvis * math::ToMat4(shoulder, none, unit)}};
}

// An Aimer Clip one second long, its Pelvis pelvis high and bent ahead by bend
// radians (back, if negative), its Arm swung up ahead by raise. With a
// stride_length, a still walk of it: its left foot lands at its start and its
// right halfway.
inline animation::Clip AimerClip(float pelvis, float bend = 0.0F, float raise = 0.0F, float stride_length = 0.0F) {
  using ozz::math::Float3;
  using ozz::math::Quaternion;
  ozz::animation::offline::RawAnimation raw;
  raw.duration = 1.0F;
  // ozz-animation's order: Root, Pelvis, Head, Arm.
  raw.tracks.resize(4);
  raw.tracks[1].translations.push_back({.time = 0.0F, .value = Float3(0.0F, pelvis, 0.0F)});
  // A turn about +X tips +Y back, to +Z, and swings -Y ahead, to -Z.
  raw.tracks[1].rotations.push_back(
      {.time = 0.0F, .value = Quaternion::FromAxisAngle(Float3(1.0F, 0.0F, 0.0F), -bend)});
  raw.tracks[2].translations.push_back({.time = 0.0F, .value = Float3(0.0F, kHeadAbovePelvis, 0.0F)});
  raw.tracks[3].translations.push_back(
      {.time = 0.0F, .value = Float3(kAimerShoulderSide, kAimerShoulderAbovePelvis, 0.0F)});
  raw.tracks[3].rotations.push_back(
      {.time = 0.0F, .value = Quaternion::FromAxisAngle(Float3(1.0F, 0.0F, 0.0F), raise)});
  auto built = ozz::animation::offline::AnimationBuilder()(raw);
  EXPECT_TRUE(built);
  animation::Clip clip{.animation = std::move(*built), .stride_length = stride_length, .foot_contacts = {}};
  if (stride_length > 0.0F) {
    clip.foot_contacts = {{.foot = assets::Foot::kLeft, .ratio = 0.0F}, {.foot = assets::Foot::kRight, .ratio = 0.5F}};
  }
  return clip;
}

// An Aimer stance's Clips, its Pelvis pelvis high: its Aim offset bends it by
// bend, its ADS grip raises its Arm by raise, and its walks, of these stride
// lengths, hold it upright.
inline animation::StanceClips AimerStanceOf(float pelvis, float bend, float raise, float forward, float backward,
                                            float left, float right, bool sides = true) {
  animation::StanceClips clips;
  clips.idle = AimerClip(pelvis);
  clips.forward = AimerClip(pelvis, 0.0F, 0.0F, forward);
  clips.backward = AimerClip(pelvis, 0.0F, 0.0F, backward);
  clips.aim_down = AimerClip(pelvis, bend);
  clips.aim_center = AimerClip(pelvis);
  clips.aim_up = AimerClip(pelvis, -bend);
  clips.ads = AimerClip(pelvis, 0.0F, raise);
  if (sides) {
    clips.left = AimerClip(pelvis, 0.0F, 0.0F, left);
    clips.right = AimerClip(pelvis, 0.0F, 0.0F, right);
  }
  return clips;
}

// The Aimer's Clips, with TestClips' stride lengths.
inline std::shared_ptr<const animation::CharacterClips> AimerClips() {
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
  pelvis.children.resize(2);
  pelvis.children[0].name = "Root/Pelvis/Head";
  pelvis.children[0].transform = ozz::math::Transform::identity();
  pelvis.children[0].transform.translation = ozz::math::Float3(0.0F, kHeadAbovePelvis, 0.0F);
  pelvis.children[1].name = "Root/Pelvis/Arm";
  pelvis.children[1].transform = ozz::math::Transform::identity();
  pelvis.children[1].transform.translation = ozz::math::Float3(kAimerShoulderSide, kAimerShoulderAbovePelvis, 0.0F);
  auto skeleton = ozz::animation::offline::SkeletonBuilder()(raw);
  EXPECT_TRUE(skeleton);

  auto clips = std::make_shared<animation::CharacterClips>();
  clips->skeleton = std::move(*skeleton);
  clips->joints = {kAimerRoot, kAimerPelvis, kAimerHead, kAimerArm};
  animation::StanceClips standing = AimerStanceOf(kPelvisStanding, kStandingAimBend, kStandingAdsRaise,
                                                  kStandingForward, kStandingBackward, kStandingLeft, kStandingRight);
  standing.sprint = AimerClip(kPelvisStanding, 0.0F, 0.0F, kStandingSprint);
  clips->stances[static_cast<std::size_t>(physics::Stance::kStanding)] = std::move(standing);
  clips->stances[static_cast<std::size_t>(physics::Stance::kCrouching)] =
      AimerStanceOf(kPelvisCrouching, kCrouchingAimBend, kCrouchingAdsRaise, kCrouchingForward, 1.2F, 1.3F, 1.3F);
  clips->stances[static_cast<std::size_t>(physics::Stance::kProne)] =
      AimerStanceOf(kPelvisProne, kProneAimBend, kProneAdsRaise, kProneForward, kProneBackward, 0.0F, 0.0F, false);
  clips->air = AimerClip(kPelvisStanding);
  clips->transitions = {.standing_crouching = 0.25F, .standing_prone = 0.8F, .crouching_prone = 0.6F};
  return clips;
}

// The Holder: the Aimer holding the rifle (ADR-0066). Its right arm is the
// Aimer's Arm, with a RightHand at its end, kHolderArm down it, and the weapon
// socket on that hand; its left arm hangs from a shoulder as far to the
// Pelvis's left, a LeftForearm kHolderUpperArm down it, bent ahead at the elbow
// by kHolderElbowBend, and a LeftHand kHolderForearm down that. Its Clips bend
// and raise it as the Aimer's do, the left arm held as it is in every one, so
// where the left hand ends up is the IK's.
inline constexpr float kHolderArm = 0.6F;
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
      {"Root/Pelvis/RightArm", kHolderPelvis, math::Vec3(kAimerShoulderSide, kAimerShoulderAbovePelvis, 0.0F)},
      {"Root/Pelvis/RightArm/RightHand", kHolderRightArm, math::Vec3(0.0F, -kHolderArm, 0.0F)},
      {"Root/Pelvis/LeftArm", kHolderPelvis, math::Vec3(-kAimerShoulderSide, kAimerShoulderAbovePelvis, 0.0F)},
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

// A Holder Clip one second long, its Pelvis pelvis high and bent ahead by bend,
// its right arm swung up ahead by raise, as AimerClip's; with a stride_length, a
// still walk of it.
inline animation::Clip HolderClip(float pelvis, float bend = 0.0F, float raise = 0.0F, float stride_length = 0.0F) {
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
  auto built = ozz::animation::offline::AnimationBuilder()(raw);
  EXPECT_TRUE(built);
  animation::Clip clip{.animation = std::move(*built), .stride_length = stride_length, .foot_contacts = {}};
  if (stride_length > 0.0F) {
    clip.foot_contacts = {{.foot = assets::Foot::kLeft, .ratio = 0.0F}, {.foot = assets::Foot::kRight, .ratio = 0.5F}};
  }
  return clip;
}

// A Holder stance's Clips, as AimerStanceOf's.
inline animation::StanceClips HolderStanceOf(float pelvis, float bend, float raise, float forward, float backward,
                                             float left, float right, bool sides = true) {
  animation::StanceClips clips;
  clips.idle = HolderClip(pelvis);
  clips.forward = HolderClip(pelvis, 0.0F, 0.0F, forward);
  clips.backward = HolderClip(pelvis, 0.0F, 0.0F, backward);
  clips.aim_down = HolderClip(pelvis, bend);
  clips.aim_center = HolderClip(pelvis);
  clips.aim_up = HolderClip(pelvis, -bend);
  clips.ads = HolderClip(pelvis, 0.0F, raise);
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

// The Holder's Clips, with TestClips' stride lengths, and its weapon socket:
// on its RightHand, unturned, its left hand its LeftHand. None unless holding,
// for a Holder holding no rifle.
inline std::shared_ptr<const animation::CharacterClips> HolderClips(bool holding = true) {
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
  if (holding) {
    clips->weapon_socket =
        animation::WeaponSocket{.joint = kHolderRightHand, .frame = math::Mat4(1.0F), .left_hand = kHolderLeftHand};
  }
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
