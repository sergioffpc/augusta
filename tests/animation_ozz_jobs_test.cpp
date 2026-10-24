#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/transform.h>
#include <ozz/base/span.h>

// ozz-animation's runtime jobs, which the Animation module poses with
// (ADR-0066), run on SIMD values held on the stack, as the module holds them:
// ozz-animation's prebuilt libraries (built by MSVC on Windows) and this build
// agree on what a SIMD value is and how it is aligned.
//
// What the module may hold them in: ozz's own structs (SoaTransform, Float4x4)
// and arrays or vectors of them, never a standard template instantiated on a
// SIMD value itself (std::array<SimdFloat4, N>, std::optional<SimdFloat4>, ...).
// MSVC's standard library declares its templates under #pragma pack(8), and
// clang-cl caps a SIMD template argument's alignment at that pack where MSVC
// does not: such a std::array is 8-aligned on the stack, and the aligned SSE
// load of its elements crashes.
namespace {

static_assert(alignof(ozz::math::SimdFloat4) == 16);
static_assert(alignof(ozz::math::SoaTransform) == 16);
static_assert(alignof(std::array<ozz::math::SoaTransform, 2>) == 16);
static_assert(alignof(std::array<ozz::math::Float4x4, 2>) == 16);

constexpr int kJoints = 5;
constexpr std::size_t kSoaJoints = (kJoints + 3) / 4;
// How far a translation ozz-animation keeps as half floats may be off, in
// meters.
constexpr float kHalfPrecision = 2e-3F;

bool AlignedTo16(const void* address) { return reinterpret_cast<std::uintptr_t>(address) % 16 == 0; }

// A chain of kJoints Joints, each 1 m above its parent.
ozz::unique_ptr<ozz::animation::Skeleton> Chain() {
  ozz::animation::offline::RawSkeleton raw;
  raw.roots.resize(1);
  ozz::animation::offline::RawSkeleton::Joint* joint = &raw.roots[0];
  for (int index = 0; index < kJoints; ++index) {
    joint->name = "joint" + std::to_string(index);
    joint->transform = ozz::math::Transform::identity();
    joint->transform.translation = ozz::math::Float3(0.0F, index == 0 ? 0.0F : 1.0F, 0.0F);
    if (index + 1 < kJoints) {
      joint->children.resize(1);
      joint = &joint->children[0];
    }
  }
  return ozz::animation::offline::SkeletonBuilder()(raw);
}

// One second over which every Joint's translation goes from (0, 1, 0) to
// (2, 1, 0), the root's from the origin to (2, 0, 0).
ozz::unique_ptr<ozz::animation::Animation> Slide() {
  ozz::animation::offline::RawAnimation raw;
  raw.duration = 1.0F;
  raw.tracks.resize(kJoints);
  for (int index = 0; index < kJoints; ++index) {
    const float up = index == 0 ? 0.0F : 1.0F;
    auto& track = raw.tracks[static_cast<std::size_t>(index)];
    track.translations.push_back({.time = 0.0F, .value = ozz::math::Float3(0.0F, up, 0.0F)});
    track.translations.push_back({.time = 1.0F, .value = ozz::math::Float3(2.0F, up, 0.0F)});
  }
  return ozz::animation::offline::AnimationBuilder()(raw);
}

TEST(AnimationOzzJobsTest, SampleBlendAndComposeOnTheStack) {
  const auto skeleton = Chain();
  const auto animation = Slide();
  ASSERT_TRUE(skeleton);
  ASSERT_TRUE(animation);
  ASSERT_EQ(static_cast<std::size_t>(skeleton->num_soa_joints()), kSoaJoints);

  ozz::animation::SamplingJob::Context context(kJoints);
  std::array<ozz::math::SoaTransform, kSoaJoints> sampled{};
  ozz::animation::SamplingJob sampling;
  sampling.animation = animation.get();
  sampling.context = &context;
  sampling.ratio = 0.5F;
  sampling.output = {sampled.data(), sampled.size()};
  ASSERT_TRUE(sampling.Run());

  std::array<ozz::math::SoaTransform, kSoaJoints> blended{};
  const std::array<ozz::animation::BlendingJob::Layer, 1> layers{
      {{.weight = 1.0F, .transform = {sampled.data(), sampled.size()}, .joint_weights = {}}}};
  ozz::animation::BlendingJob blending;
  blending.layers = {layers.data(), layers.size()};
  blending.rest_pose = skeleton->joint_rest_poses();
  blending.output = {blended.data(), blended.size()};
  ASSERT_TRUE(blending.Run());

  std::array<ozz::math::Float4x4, kJoints> models{};
  ozz::animation::LocalToModelJob local_to_model;
  local_to_model.skeleton = skeleton.get();
  local_to_model.input = {blended.data(), blended.size()};
  local_to_model.output = {models.data(), models.size()};
  ASSERT_TRUE(local_to_model.Run());

  EXPECT_TRUE(AlignedTo16(sampled.data()));
  EXPECT_TRUE(AlignedTo16(blended.data()));
  EXPECT_TRUE(AlignedTo16(models.data()));
  // The first four Joints' x, each slid halfway, read lane by lane.
  std::array<float, 4> xs{};
  ozz::math::StorePtrU(blended[0].translation.x, xs.data());
  for (const float x : xs) {
    EXPECT_NEAR(x, 1.0F, kHalfPrecision);
  }
  // The last Joint, four up the chain, each slid 1 m along x.
  EXPECT_NEAR(ozz::math::GetX(models[kJoints - 1].cols[3]), 5.0F, kHalfPrecision * kJoints);
  EXPECT_NEAR(ozz::math::GetY(models[kJoints - 1].cols[3]), 4.0F, kHalfPrecision * kJoints);
}

}  // namespace
