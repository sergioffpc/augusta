#include "augusta/clips.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/physics.h"

namespace augusta::animation {

namespace {

// Each stance's name, as a Clip's role begins with it, indexed by
// physics::Stance.
constexpr std::array<std::string_view, physics::kStanceCount> kStanceNames = {"standing", "crouching", "prone"};

// The refusal of part for cause, about the Clip of role for a ClipPart::kClip;
// a cause that names more has its caller set it.
ClipLoadError Refusal(ClipPart part, ClipLoadCause cause, std::string role = {}) {
  return ClipLoadError{.part = part,
                       .cause = cause,
                       .role = std::move(role),
                       .resolve = assets::ResolveError::kNotFound,
                       .joint = {},
                       .count = 0,
                       .expected = 0};
}

// The refusal of part, which the pack could not give for error.
ClipLoadError Unresolved(ClipPart part, assets::ResolveError error, std::string role = {}) {
  ClipLoadError refusal = Refusal(part, ClipLoadCause::kUnresolved, std::move(role));
  refusal.resolve = error;
  return refusal;
}

// The refusal of part for cause, which names joint.
ClipLoadError Naming(ClipPart part, ClipLoadCause cause, std::string_view joint) {
  ClipLoadError refusal = Refusal(part, cause);
  refusal.joint = joint;
  return refusal;
}

// The refusal of part for cause, count found where expected were.
ClipLoadError Counting(ClipPart part, ClipLoadCause cause, std::size_t count, std::size_t expected,
                       std::string role = {}) {
  ClipLoadError refusal = Refusal(part, cause, std::move(role));
  refusal.count = count;
  refusal.expected = expected;
  return refusal;
}

// object out of archive, an ozz-animation archive of one; nullopt when it is none.
template <typename Object>
std::optional<Object> Unarchive(std::span<const std::byte> archive) {
  ozz::io::MemoryStream stream;
  stream.Write(archive.data(), archive.size());
  stream.Seek(0, ozz::io::Stream::kSet);
  ozz::io::IArchive input(&stream);
  if (!input.TestTag<Object>()) {
    return std::nullopt;
  }
  Object object;
  input >> object;
  return object;
}

std::expected<ozz::animation::Skeleton, ClipLoadError> LoadSkeleton(const assets::Pack& pack,
                                                                    std::string_view character) {
  const auto archive = pack.ResolveAnimationSkeleton(character);
  if (!archive) {
    return std::unexpected(Unresolved(ClipPart::kSkeleton, archive.error()));
  }
  auto skeleton = Unarchive<ozz::animation::Skeleton>(*archive);
  if (!skeleton) {
    return std::unexpected(Refusal(ClipPart::kSkeleton, ClipLoadCause::kNotAnArchive));
  }
  if (skeleton->num_joints() == 0) {
    return std::unexpected(Refusal(ClipPart::kSkeleton, ClipLoadCause::kNoJoint));
  }
  return *std::move(skeleton);
}

// The index of the Joint named name in skeleton; nullopt when it has none.
std::optional<std::uint32_t> IndexOf(const assets::Skeleton& skeleton, std::string_view name) {
  for (std::size_t joint = 0; joint < skeleton.joints.size(); ++joint) {
    if (skeleton.joints[joint].name == name) {
      return static_cast<std::uint32_t>(joint);
    }
  }
  return std::nullopt;
}

// For each Joint of animated, its index in skeleton, found by name, or why
// the two do not hold the same Joints.
std::expected<std::vector<std::uint32_t>, ClipLoadError> JointsOf(const ozz::animation::Skeleton& animated,
                                                                  const assets::Skeleton& skeleton) {
  std::unordered_map<std::string_view, std::uint32_t> index_of;
  for (std::size_t joint = 0; joint < skeleton.joints.size(); ++joint) {
    index_of.emplace(skeleton.joints[joint].name, static_cast<std::uint32_t>(joint));
  }
  std::vector<std::uint32_t> joints;
  for (const char* const name : animated.joint_names()) {
    const auto found = index_of.find(name);
    if (found == index_of.end()) {
      return std::unexpected(Naming(ClipPart::kSkeleton, ClipLoadCause::kUnknownJoint, name));
    }
    joints.push_back(found->second);
  }
  if (joints.size() != skeleton.joints.size()) {
    return std::unexpected(
        Counting(ClipPart::kSkeleton, ClipLoadCause::kJointCountMismatch, joints.size(), skeleton.joints.size()));
  }
  return joints;
}

// The Clip of character that plays role, on skeleton; nullopt when the pack has
// none and optional is set.
std::expected<std::optional<Clip>, ClipLoadError> LoadClip(const assets::Pack& pack, std::string_view character,
                                                           const ozz::animation::Skeleton& skeleton,
                                                           const std::string& role, bool optional) {
  auto data = pack.ResolveClip(character, role);
  if (!data) {
    if (optional && data.error() == assets::ResolveError::kNotFound) {
      return std::nullopt;
    }
    return std::unexpected(Unresolved(ClipPart::kClip, data.error(), role));
  }
  auto animation = Unarchive<ozz::animation::Animation>(data->archive);
  if (!animation) {
    return std::unexpected(Refusal(ClipPart::kClip, ClipLoadCause::kNotAnArchive, role));
  }
  if (animation->num_tracks() != skeleton.num_joints()) {
    return std::unexpected(Counting(ClipPart::kClip, ClipLoadCause::kTrackCountMismatch,
                                    static_cast<std::size_t>(animation->num_tracks()),
                                    static_cast<std::size_t>(skeleton.num_joints()), role));
  }
  return Clip{.animation = *std::move(animation),
              .stride_length = data->stride_length,
              .foot_contacts = std::move(data->foot_contacts)};
}

// Loads stance's Clips of character into clips.
std::expected<void, ClipLoadError> LoadStance(const assets::Pack& pack, std::string_view character,
                                              const ozz::animation::Skeleton& skeleton, physics::Stance stance,
                                              StanceClips& clips) {
  const std::string_view name = kStanceNames[static_cast<std::size_t>(stance)];
  const auto role = [name](std::string_view clip) { return std::format("{}/{}", name, clip); };
  const auto required = [&](std::string_view clip, Clip& into) -> std::expected<void, ClipLoadError> {
    auto loaded = LoadClip(pack, character, skeleton, role(clip), false);
    if (!loaded) {
      return std::unexpected(std::move(loaded.error()));
    }
    into = **std::move(loaded);
    return {};
  };
  const auto either = [&](std::string_view clip, bool is_optional,
                          std::optional<Clip>& into) -> std::expected<void, ClipLoadError> {
    auto loaded = LoadClip(pack, character, skeleton, role(clip), is_optional);
    if (!loaded) {
      return std::unexpected(std::move(loaded.error()));
    }
    into = *std::move(loaded);
    return {};
  };
  const bool prone = stance == physics::Stance::kProne;
  return required("idle", clips.idle)
      .and_then([&] { return required("forward", clips.forward); })
      .and_then([&] { return required("backward", clips.backward); })
      .and_then([&] { return either("left", prone, clips.left); })
      .and_then([&] { return either("right", prone, clips.right); })
      .and_then([&] { return either("forward_left", true, clips.forward_left); })
      .and_then([&] { return either("forward_right", true, clips.forward_right); })
      .and_then([&] { return either("backward_left", true, clips.backward_left); })
      .and_then([&] { return either("backward_right", true, clips.backward_right); })
      .and_then([&]() -> std::expected<void, ClipLoadError> {
        if (stance != physics::Stance::kStanding) {
          return {};
        }
        return either("sprint", false, clips.sprint);
      })
      .and_then([&] { return required("aim_down", clips.aim_down); })
      .and_then([&] { return required("aim_center", clips.aim_center); })
      .and_then([&] { return required("aim_up", clips.aim_up); })
      .and_then([&] { return required("ads", clips.ads); });
}

// Whether hand, a Joint of skeleton, has an arm: a parent, the elbow, and a
// grandparent, the shoulder.
bool HasAnArm(const assets::Skeleton& skeleton, std::uint32_t hand) {
  const std::uint32_t elbow = skeleton.joints[hand].parent;
  return elbow != assets::kJointNoParent && skeleton.joints[elbow].parent != assets::kJointNoParent;
}

// The Weapon socket of character, its Joints found by name in skeleton.
std::expected<WeaponSocket, ClipLoadError> LoadWeaponSocket(const assets::Pack& pack, std::string_view character,
                                                            const assets::Skeleton& skeleton) {
  const auto data = pack.ResolveWeaponSocket(character);
  if (!data) {
    return std::unexpected(Unresolved(ClipPart::kWeaponSocket, data.error()));
  }
  const auto right_hand = IndexOf(skeleton, data->right_hand);
  if (!right_hand) {
    return std::unexpected(Naming(ClipPart::kWeaponSocket, ClipLoadCause::kUnknownRightHand, data->right_hand));
  }
  const auto left_hand = IndexOf(skeleton, data->left_hand);
  if (!left_hand) {
    return std::unexpected(Naming(ClipPart::kWeaponSocket, ClipLoadCause::kUnknownLeftHand, data->left_hand));
  }
  if (!HasAnArm(skeleton, *left_hand)) {
    return std::unexpected(Naming(ClipPart::kWeaponSocket, ClipLoadCause::kLeftHandHasNoArm, data->left_hand));
  }
  if (!HasAnArm(skeleton, *right_hand)) {
    return std::unexpected(Naming(ClipPart::kWeaponSocket, ClipLoadCause::kRightHandHasNoArm, data->right_hand));
  }
  return WeaponSocket{.right_hand = *right_hand,
                      .frame = math::ToMat4(data->translation, data->rotation, math::Vec3(1.0F)),
                      .left_hand = *left_hand};
}

}  // namespace

const StanceClips& CharacterClips::Of(physics::Stance stance) const {
  return stances[static_cast<std::size_t>(stance)];
}

std::uint32_t ShoulderOf(const assets::Skeleton& skeleton, const WeaponSocket& socket) {
  return skeleton.joints.at(skeleton.joints.at(socket.right_hand).parent).parent;
}

namespace {

// What part is called in a message, as DescribeResolveError names it too.
std::string_view NameOf(ClipPart part) {
  switch (part) {
    case ClipPart::kSkeleton:
      return "animation skeleton";
    case ClipPart::kClip:
      return "clip";
    case ClipPart::kTransitions:
      return "stance transitions";
    case ClipPart::kWeaponSocket:
      return "weapon socket";
  }
  return "clips";
}

// Why error's part did not load, as a phrase.
std::string CauseOf(const ClipLoadError& error) {
  switch (error.cause) {
    case ClipLoadCause::kUnresolved:
      return assets::DescribeResolveError(error.resolve, NameOf(error.part));
    case ClipLoadCause::kNotAnArchive:
      return "is not an ozz-animation archive of its kind";
    case ClipLoadCause::kNoJoint:
      return "has no joint";
    case ClipLoadCause::kUnknownJoint:
      return std::format("has a joint {} the character's skeleton lacks", error.joint);
    case ClipLoadCause::kJointCountMismatch:
      return std::format("has {} joints, the character's skeleton {}", error.count, error.expected);
    case ClipLoadCause::kTrackCountMismatch:
      return std::format("has {} tracks for a skeleton of {} joints", error.count, error.expected);
    case ClipLoadCause::kUnknownRightHand:
      return std::format("names a right hand {} the character's skeleton lacks", error.joint);
    case ClipLoadCause::kUnknownLeftHand:
      return std::format("names a left hand {} the character's skeleton lacks", error.joint);
    case ClipLoadCause::kLeftHandHasNoArm:
      return std::format("left hand {} has no arm: a parent and a grandparent", error.joint);
    case ClipLoadCause::kRightHandHasNoArm:
      return std::format("right hand {} has no arm: a parent and a grandparent", error.joint);
  }
  return "does not load";
}

}  // namespace

std::string DescribeClipLoadError(const ClipLoadError& error) {
  if (error.part == ClipPart::kClip) {
    return std::format("clip {}: {}", error.role, CauseOf(error));
  }
  return std::format("{}: {}", NameOf(error.part), CauseOf(error));
}

std::expected<CharacterClips, ClipLoadError> LoadClips(const assets::Pack& pack, std::string_view character,
                                                       const assets::Skeleton& skeleton) {
  CharacterClips clips;
  auto animated = LoadSkeleton(pack, character);
  if (!animated) {
    return std::unexpected(std::move(animated.error()));
  }
  clips.skeleton = *std::move(animated);
  auto joints = JointsOf(clips.skeleton, skeleton);
  if (!joints) {
    return std::unexpected(std::move(joints.error()));
  }
  clips.joints = *std::move(joints);
  for (std::size_t stance = 0; stance < physics::kStanceCount; ++stance) {
    if (auto loaded =
            LoadStance(pack, character, clips.skeleton, static_cast<physics::Stance>(stance), clips.stances[stance]);
        !loaded) {
      return std::unexpected(std::move(loaded.error()));
    }
  }
  auto air = LoadClip(pack, character, clips.skeleton, "air", false);
  if (!air) {
    return std::unexpected(std::move(air.error()));
  }
  clips.air = **std::move(air);
  const auto transitions = pack.ResolveStanceTransitions(character);
  if (!transitions) {
    return std::unexpected(Unresolved(ClipPart::kTransitions, transitions.error()));
  }
  clips.transitions = *transitions;
  auto socket = LoadWeaponSocket(pack, character, skeleton);
  if (!socket) {
    return std::unexpected(std::move(socket.error()));
  }
  clips.weapon_socket = *socket;
  return clips;
}

}  // namespace augusta::animation
