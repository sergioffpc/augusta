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
#include "augusta/physics.h"

namespace augusta::animation {

namespace {

// Each stance's name, as a Clip's role begins with it, indexed by
// physics::Stance.
constexpr std::array<std::string_view, physics::kStanceCount> kStanceNames = {"standing", "crouching", "prone"};

// object out of archive, an ozz-animation archive of one, or why it is none.
template <typename Object>
std::expected<Object, std::string> Unarchive(std::span<const std::byte> archive) {
  ozz::io::MemoryStream stream;
  stream.Write(archive.data(), archive.size());
  stream.Seek(0, ozz::io::Stream::kSet);
  ozz::io::IArchive input(&stream);
  if (!input.TestTag<Object>()) {
    return std::unexpected("is not an ozz-animation archive of its kind");
  }
  Object object;
  input >> object;
  return object;
}

std::expected<ozz::animation::Skeleton, ClipLoadError> LoadSkeleton(const assets::Pack& pack,
                                                                    std::string_view character) {
  const auto failed = [](std::string reason) {
    return std::unexpected(ClipLoadError{.part = ClipPart::kSkeleton, .role = {}, .reason = std::move(reason)});
  };
  const auto archive = pack.ResolveAnimationSkeleton(character);
  if (!archive) {
    return failed(assets::DescribeResolveError(archive.error(), "animation skeleton"));
  }
  auto skeleton = Unarchive<ozz::animation::Skeleton>(*archive);
  if (!skeleton) {
    return failed(std::move(skeleton.error()));
  }
  if (skeleton->num_joints() == 0) {
    return failed("has no joint");
  }
  return *std::move(skeleton);
}

// For each Joint of animated, its index in skeleton, found by name, or why
// the two do not hold the same Joints.
std::expected<std::vector<std::uint32_t>, ClipLoadError> JointsOf(const ozz::animation::Skeleton& animated,
                                                                  const assets::Skeleton& skeleton) {
  const auto failed = [](std::string reason) {
    return std::unexpected(ClipLoadError{.part = ClipPart::kSkeleton, .role = {}, .reason = std::move(reason)});
  };
  std::unordered_map<std::string_view, std::uint32_t> index_of;
  for (std::size_t joint = 0; joint < skeleton.joints.size(); ++joint) {
    index_of.emplace(skeleton.joints[joint].name, static_cast<std::uint32_t>(joint));
  }
  std::vector<std::uint32_t> joints;
  for (const char* const name : animated.joint_names()) {
    const auto found = index_of.find(name);
    if (found == index_of.end()) {
      return failed(std::format("has a joint {} the character's skeleton lacks", name));
    }
    joints.push_back(found->second);
  }
  if (joints.size() != skeleton.joints.size()) {
    return failed(std::format("has {} joints, the character's skeleton {}", joints.size(), skeleton.joints.size()));
  }
  return joints;
}

// The Clip of character that plays role, on skeleton; nullopt when the pack has
// none and optional is set.
std::expected<std::optional<Clip>, ClipLoadError> LoadClip(const assets::Pack& pack, std::string_view character,
                                                           const ozz::animation::Skeleton& skeleton,
                                                           const std::string& role, bool optional) {
  const auto failed = [&role](std::string reason) {
    return std::unexpected(ClipLoadError{.part = ClipPart::kClip, .role = role, .reason = std::move(reason)});
  };
  auto data = pack.ResolveClip(character, role);
  if (!data) {
    if (optional && data.error() == assets::ResolveError::kNotFound) {
      return std::nullopt;
    }
    return failed(assets::DescribeResolveError(data.error(), "clip"));
  }
  auto animation = Unarchive<ozz::animation::Animation>(data->archive);
  if (!animation) {
    return failed(std::move(animation.error()));
  }
  if (animation->num_tracks() != skeleton.num_joints()) {
    return failed(
        std::format("has {} tracks for a skeleton of {} joints", animation->num_tracks(), skeleton.num_joints()));
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

}  // namespace

const StanceClips& CharacterClips::Of(physics::Stance stance) const {
  return stances[static_cast<std::size_t>(stance)];
}

float CharacterClips::TransitionDuration(physics::Stance from, physics::Stance to) const {
  return physics::TransitionDuration(transitions, from, to);
}

std::string DescribeClipLoadError(const ClipLoadError& error) {
  switch (error.part) {
    case ClipPart::kSkeleton:
      return std::format("animation skeleton: {}", error.reason);
    case ClipPart::kClip:
      return std::format("clip {}: {}", error.role, error.reason);
    case ClipPart::kTransitions:
      return std::format("stance transitions: {}", error.reason);
  }
  return error.reason;
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
    return std::unexpected(
        ClipLoadError{.part = ClipPart::kTransitions,
                      .role = {},
                      .reason = assets::DescribeResolveError(transitions.error(), "stance transitions")});
  }
  clips.transitions = *transitions;
  return clips;
}

}  // namespace augusta::animation
