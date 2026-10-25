#include "augusta/clips.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/transform.h>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/pack_encoder.h"
#include "augusta/physics.h"

// A Character's Clips loaded out of a pack (ADR-0066), on Clips each test
// authors with ozz-animation's offline libraries, as the cooker does: by role,
// with their stride lengths and the stance transitions, or refused naming the
// part that is missing or does not load.
namespace {

using augusta::animation::CharacterClips;
using augusta::animation::ClipLoadCause;
using augusta::animation::ClipLoadError;
using augusta::animation::ClipPart;
using augusta::animation::LoadClips;
using augusta::assets::AssetEntry;
using augusta::assets::AssetType;
using augusta::assets::Foot;
using augusta::physics::Stance;

constexpr std::string_view kCharacter = "soldier";

// Every role the Hitbox pose needs a Clip for (ADR-0066), and the stride length
// each one's test Clip measured: locomotion strides, 0 for the rest.
const std::map<std::string, float, std::less<>> kRequiredRoles = {
    {"standing/idle", 0.0F},
    {"standing/forward", 1.4F},
    {"standing/backward", 1.0F},
    {"standing/left", 1.1F},
    {"standing/right", 1.2F},
    {"standing/sprint", 2.4F},
    {"standing/aim_down", 0.0F},
    {"standing/aim_center", 0.0F},
    {"standing/aim_up", 0.0F},
    {"standing/ads", 0.0F},
    {"crouching/idle", 0.0F},
    {"crouching/forward", 0.9F},
    {"crouching/backward", 0.7F},
    {"crouching/left", 0.8F},
    {"crouching/right", 0.8F},
    {"crouching/aim_down", 0.0F},
    {"crouching/aim_center", 0.0F},
    {"crouching/aim_up", 0.0F},
    {"crouching/ads", 0.0F},
    {"prone/idle", 0.0F},
    {"prone/forward", 0.5F},
    {"prone/backward", 0.4F},
    {"prone/aim_down", 0.0F},
    {"prone/aim_center", 0.0F},
    {"prone/aim_up", 0.0F},
    {"prone/ads", 0.0F},
    {"air", 0.0F},
};

// The Character's Skeleton, parents before children: its foot listed after its
// spine, where ozz-animation keeps the foot (depth first) before it.
augusta::assets::Skeleton Soldier() {
  return {.joints = {{.name = "Root"},
                     {.name = "Root/LeftLeg", .parent = 0},
                     {.name = "Root/Spine", .parent = 0},
                     {.name = "Root/LeftLeg/Foot", .parent = 1}},
          .rest_transforms = {}};
}

template <typename Object>
std::vector<std::byte> ArchiveOf(const Object& object) {
  ozz::io::MemoryStream stream;
  ozz::io::OArchive archive(&stream);
  archive << object;
  std::vector<std::byte> bytes(stream.Size());
  stream.Seek(0, ozz::io::Stream::kSet);
  stream.Read(bytes.data(), bytes.size());
  return bytes;
}

// The RawSkeleton Joint of joints[index], each of its children under it.
ozz::animation::offline::RawSkeleton::Joint RawJointOf(std::span<const augusta::assets::JointData> joints,
                                                       std::size_t index) {
  ozz::animation::offline::RawSkeleton::Joint raw;
  raw.name = joints[index].name.c_str();
  raw.transform = ozz::math::Transform::identity();
  for (std::size_t child = 0; child < joints.size(); ++child) {
    if (joints[child].parent == index) {
      raw.children.push_back(RawJointOf(joints, child));
    }
  }
  return raw;
}

// The ozz-animation Skeleton archive of joints, as the cooker builds it.
std::vector<std::byte> SkeletonArchiveOf(std::span<const augusta::assets::JointData> joints) {
  ozz::animation::offline::RawSkeleton raw;
  raw.roots.push_back(RawJointOf(joints, 0));
  const auto skeleton = ozz::animation::offline::SkeletonBuilder()(raw);
  EXPECT_TRUE(skeleton);
  return ArchiveOf(*skeleton);
}

// A one-second Animation archive of tracks Joints, each still.
std::vector<std::byte> ClipArchive(std::size_t tracks) {
  ozz::animation::offline::RawAnimation raw;
  raw.duration = 1.0F;
  raw.tracks.resize(tracks);
  const auto animation = ozz::animation::offline::AnimationBuilder()(raw);
  EXPECT_TRUE(animation);
  return ArchiveOf(*animation);
}

AssetEntry ClipEntry(std::string_view role, float stride_length, std::vector<std::byte> archive) {
  std::vector<augusta::assets::FootContact> contacts;
  if (stride_length > 0.0F) {
    contacts = {{.foot = Foot::kLeft, .ratio = 0.0F}, {.foot = Foot::kRight, .ratio = 0.5F}};
  }
  const auto blob = augusta::assets::EncodeClipBlob(
      {.stride_length = stride_length, .foot_contacts = std::move(contacts), .archive = std::move(archive)});
  EXPECT_TRUE(blob.has_value());
  return {.type = AssetType::kClip,
          .path = augusta::assets::CharacterClipPath(kCharacter, role),
          .data = blob.value_or(std::vector<std::byte>{})};
}

// The Character's Weapon socket: on its spine, 10 cm ahead of it, its left hand
// at its foot, whose leg is the arm.
AssetEntry WeaponSocketEntry(std::string_view left_hand = "Root/LeftLeg/Foot") {
  const auto blob = augusta::assets::EncodeWeaponSocketBlob({.right_hand = "Root/Spine",
                                                             .translation = augusta::math::Vec3(0.0F, 0.0F, -0.1F),
                                                             .rotation = augusta::math::Quat(1.0F, 0.0F, 0.0F, 0.0F),
                                                             .left_hand = std::string(left_hand)});
  EXPECT_TRUE(blob.has_value());
  return {.type = AssetType::kWeaponSocket,
          .path = augusta::assets::CharacterWeaponSocketPath(kCharacter),
          .data = blob.value_or(std::vector<std::byte>{})};
}

class ClipsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto skeleton = augusta::assets::EncodeAnimationSkeletonBlob(SkeletonArchiveOf(Soldier().joints));
    const auto transitions = augusta::assets::EncodeStanceTransitionsBlob(
        {.standing_crouching = 0.25F, .standing_prone = 0.8F, .crouching_prone = 0.6F});
    ASSERT_TRUE(skeleton.has_value() && transitions.has_value());
    entries_ = {{.type = AssetType::kAnimationSkeleton,
                 .path = augusta::assets::CharacterAnimationSkeletonPath(kCharacter),
                 .data = *skeleton},
                {.type = AssetType::kStanceTransitions,
                 .path = augusta::assets::CharacterStanceTransitionsPath(kCharacter),
                 .data = *transitions}};
    for (const auto& [role, stride_length] : kRequiredRoles) {
      entries_.push_back(ClipEntry(role, stride_length, ClipArchive(Soldier().joints.size())));
    }
    entries_.push_back(WeaponSocketEntry());
  }

  void TearDown() override { std::filesystem::remove(path_); }

  // Removes the entry at the path of the Clip of role, or at path.
  void Remove(std::string_view path) {
    std::erase_if(entries_, [path](const AssetEntry& entry) { return entry.path == path; });
  }
  void RemoveClip(std::string_view role) { Remove(augusta::assets::CharacterClipPath(kCharacter, role)); }

  // Replaces the Clip of role.
  void Replace(std::string_view role, std::vector<std::byte> archive) {
    RemoveClip(role);
    entries_.push_back(ClipEntry(role, 0.0F, std::move(archive)));
  }

  // The Clips of kCharacter out of a pack of entries_.
  std::expected<CharacterClips, ClipLoadError> Load() {
    const auto keys = augusta::assets::GenerateEd25519KeyPair();
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path() /
            (std::string("augusta_clips_test_") + test.name() + std::string(augusta::assets::kPackExtension));
    EXPECT_TRUE(augusta::assets::WritePack(path_, entries_, keys.private_key).has_value());
    const auto pack = augusta::assets::Pack::Load(path_, keys.public_key);
    EXPECT_TRUE(pack.has_value());
    if (!pack) {
      // The pack's own failure is the one reported above.
      return std::unexpected(ClipLoadError{});
    }
    return LoadClips(*pack, kCharacter, Soldier());
  }

  std::vector<AssetEntry> entries_;
  std::filesystem::path path_;
};

TEST_F(ClipsTest, ACharactersClipsLoadByRoleWithTheirStrideLengths) {
  const auto clips = Load();

  ASSERT_TRUE(clips.has_value()) << augusta::animation::DescribeClipLoadError(clips.error());
  EXPECT_FLOAT_EQ(clips->Of(Stance::kStanding).forward.stride_length, 1.4F);
  ASSERT_TRUE(clips->Of(Stance::kStanding).sprint.has_value());
  EXPECT_FLOAT_EQ(clips->Of(Stance::kStanding).sprint->stride_length, 2.4F);
  ASSERT_TRUE(clips->Of(Stance::kCrouching).left.has_value());
  EXPECT_FLOAT_EQ(clips->Of(Stance::kCrouching).left->stride_length, 0.8F);
  EXPECT_FLOAT_EQ(clips->Of(Stance::kProne).backward.stride_length, 0.4F);
  EXPECT_FLOAT_EQ(clips->Of(Stance::kStanding).idle.stride_length, 0.0F);
  ASSERT_EQ(clips->Of(Stance::kStanding).forward.foot_contacts.size(), 2U);
  EXPECT_EQ(clips->Of(Stance::kStanding).forward.foot_contacts[1].foot, Foot::kRight);
  EXPECT_FLOAT_EQ(clips->Of(Stance::kProne).aim_up.animation.duration(), 1.0F);
  EXPECT_EQ(clips->air.animation.num_tracks(), 4);
}

TEST_F(ClipsTest, AnOptionalClipACharacterLacksIsNone) {
  entries_.push_back(ClipEntry("standing/forward_left", 1.3F, ClipArchive(Soldier().joints.size())));

  const auto clips = Load();

  ASSERT_TRUE(clips.has_value()) << augusta::animation::DescribeClipLoadError(clips.error());
  ASSERT_TRUE(clips->Of(Stance::kStanding).forward_left.has_value());
  EXPECT_FLOAT_EQ(clips->Of(Stance::kStanding).forward_left->stride_length, 1.3F);
  EXPECT_FALSE(clips->Of(Stance::kStanding).backward_right.has_value());
  EXPECT_FALSE(clips->Of(Stance::kProne).left.has_value());
  EXPECT_FALSE(clips->Of(Stance::kCrouching).sprint.has_value());
}

TEST_F(ClipsTest, EachPairOfStancesTakesItsTransitionDuration) {
  const auto clips = Load();

  ASSERT_TRUE(clips.has_value()) << augusta::animation::DescribeClipLoadError(clips.error());
  EXPECT_FLOAT_EQ(clips->transitions.standing_crouching, 0.25F);
  EXPECT_FLOAT_EQ(clips->transitions.standing_prone, 0.8F);
  EXPECT_FLOAT_EQ(clips->transitions.crouching_prone, 0.6F);
}

// ozz-animation keeps the Joints depth first: each is mapped to the Character's
// Skeleton by name.
TEST_F(ClipsTest, EachJointOfTheAnimationSkeletonIsFoundInTheCharactersSkeleton) {
  const auto clips = Load();

  ASSERT_TRUE(clips.has_value()) << augusta::animation::DescribeClipLoadError(clips.error());
  ASSERT_EQ(clips->joints.size(), 4U);
  const auto names = clips->skeleton.joint_names();
  for (std::size_t joint = 0; joint < clips->joints.size(); ++joint) {
    EXPECT_EQ(Soldier().joints[clips->joints[joint]].name, names[joint]);
  }
  EXPECT_EQ(clips->joints, (std::vector<std::uint32_t>{0, 1, 3, 2}));
}

TEST_F(ClipsTest, AMissingRequiredClipIsRefusedNamingIt) {
  RemoveClip("crouching/right");

  const auto clips = Load();

  ASSERT_FALSE(clips.has_value());
  EXPECT_EQ(clips.error().role, "crouching/right");
  EXPECT_EQ(clips.error().cause, ClipLoadCause::kUnresolved);
  EXPECT_EQ(clips.error().resolve, augusta::assets::ResolveError::kNotFound);
  EXPECT_EQ(augusta::animation::DescribeClipLoadError(clips.error()), "clip crouching/right: not found");
}

TEST_F(ClipsTest, AClipThatIsNoAnimationIsRefusedNamingIt) {
  Replace("standing/aim_up", SkeletonArchiveOf(Soldier().joints));

  const auto clips = Load();

  ASSERT_FALSE(clips.has_value());
  EXPECT_EQ(clips.error().role, "standing/aim_up");
  EXPECT_EQ(clips.error().cause, ClipLoadCause::kNotAnArchive);
}

TEST_F(ClipsTest, AClipOfAnotherSkeletonIsRefusedNamingIt) {
  Replace("prone/idle", ClipArchive(3));

  const auto clips = Load();

  ASSERT_FALSE(clips.has_value());
  EXPECT_EQ(clips.error().role, "prone/idle");
  EXPECT_EQ(clips.error().cause, ClipLoadCause::kTrackCountMismatch);
  EXPECT_EQ(clips.error().count, 3U);
  EXPECT_EQ(clips.error().expected, 4U);
  EXPECT_EQ(augusta::animation::DescribeClipLoadError(clips.error()),
            "clip prone/idle: has 3 tracks for a skeleton of 4 joints");
}

TEST_F(ClipsTest, ACharacterWithoutAnAnimationSkeletonIsRefused) {
  Remove(augusta::assets::CharacterAnimationSkeletonPath(kCharacter));

  const auto clips = Load();

  ASSERT_FALSE(clips.has_value());
  EXPECT_EQ(clips.error().part, ClipPart::kSkeleton);
}

TEST_F(ClipsTest, AnAnimationSkeletonWhoseJointsAreNotTheCharactersIsRefused) {
  Remove(augusta::assets::CharacterAnimationSkeletonPath(kCharacter));
  auto joints = Soldier().joints;
  joints[2].name = "Root/Neck";
  const auto skeleton = augusta::assets::EncodeAnimationSkeletonBlob(SkeletonArchiveOf(joints));
  ASSERT_TRUE(skeleton.has_value());
  entries_.push_back({.type = AssetType::kAnimationSkeleton,
                      .path = augusta::assets::CharacterAnimationSkeletonPath(kCharacter),
                      .data = *skeleton});

  const auto clips = Load();

  ASSERT_FALSE(clips.has_value());
  EXPECT_EQ(clips.error().part, ClipPart::kSkeleton);
  EXPECT_EQ(clips.error().cause, ClipLoadCause::kUnknownJoint);
  EXPECT_EQ(clips.error().joint, "Root/Neck");
  EXPECT_EQ(augusta::animation::DescribeClipLoadError(clips.error()),
            "animation skeleton: has a joint Root/Neck the character's skeleton lacks");
}

// The socket's Joints by their index in the Character's Skeleton: the spine
// third, the foot fourth.
TEST_F(ClipsTest, TheWeaponSocketLoadsOnItsJointWithTheLeftHand) {
  const auto clips = Load();

  ASSERT_TRUE(clips.has_value());
  EXPECT_EQ(clips->weapon_socket.right_hand, 2U);
  EXPECT_EQ(clips->weapon_socket.left_hand, 3U);
  EXPECT_EQ(augusta::math::TranslationOf(clips->weapon_socket.frame), augusta::math::Vec3(0.0F, 0.0F, -0.1F));
}

TEST_F(ClipsTest, ACharacterWithoutAWeaponSocketIsRefused) {
  Remove(augusta::assets::CharacterWeaponSocketPath(kCharacter));

  const auto clips = Load();

  ASSERT_FALSE(clips.has_value());
  EXPECT_EQ(clips.error().part, ClipPart::kWeaponSocket);
  EXPECT_EQ(augusta::animation::DescribeClipLoadError(clips.error()), "weapon socket: not found");
}

// The leg's parent is the root, which has none: no arm to bend.
TEST_F(ClipsTest, ALeftHandWithoutAnArmIsRefused) {
  Remove(augusta::assets::CharacterWeaponSocketPath(kCharacter));
  entries_.push_back(WeaponSocketEntry("Root/LeftLeg"));

  const auto clips = Load();

  ASSERT_FALSE(clips.has_value());
  EXPECT_EQ(clips.error().part, ClipPart::kWeaponSocket);
  EXPECT_EQ(clips.error().cause, ClipLoadCause::kLeftHandHasNoArm);
  EXPECT_EQ(clips.error().joint, "Root/LeftLeg");
  EXPECT_EQ(augusta::animation::DescribeClipLoadError(clips.error()),
            "weapon socket: left hand Root/LeftLeg has no arm: a parent and a grandparent");
}

TEST_F(ClipsTest, ACharacterWithoutStanceTransitionsIsRefused) {
  Remove(augusta::assets::CharacterStanceTransitionsPath(kCharacter));

  const auto clips = Load();

  ASSERT_FALSE(clips.has_value());
  EXPECT_EQ(clips.error().part, ClipPart::kTransitions);
  EXPECT_EQ(augusta::animation::DescribeClipLoadError(clips.error()), "stance transitions: not found");
}

}  // namespace
