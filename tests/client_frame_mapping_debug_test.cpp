#include <utility>

#include <gtest/gtest.h>

#include "augusta/body_part.h"
#include "augusta/harness.h"
#include "augusta/interpolation.h"
#include "augusta/link.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/physics_overlays.h"
#include "augusta/presentation.h"
#include "frame_mapping.h"

// The Development build's conversions at ClientRuntime's edge (ADR-0074).
namespace {

using augusta::body_part::BodyPart;
using augusta::client::ToPresentation;
using augusta::math::Vec3;
using augusta::physics::Stance;

TEST(FrameMappingTest, AShotKeepsTheTickItWasFiredOn) {
  const augusta::harness::Shot shot{.shooter = augusta::harness::EntityId{3}, .tick = 41, .origin = {}};

  EXPECT_EQ(ToPresentation(shot).tick, 41U);
}

TEST(FrameMappingTest, AShotJudgementKeepsEverythingTheOverlayDrawsFrom) {
  const augusta::harness::ShotJudgement judgement{
      .shot_tick = 120,
      .shooters_delay = 6.5F,
      .struck = augusta::link::Struck::kHitbox,
      .part = BodyPart::kHead,
      .point = Vec3(1.0F, 1.7F, -4.0F),
      .target = augusta::harness::EntityId{2},
      .entries = {{.ticks_after_shot = 1,
                   .players = {{.entity = augusta::harness::EntityId{2},
                                .position = Vec3(1.0F, 0.0F, -4.0F),
                                .stance = Stance::kCrouching,
                                .yaw = 0.25F}}}},
      .entries_left_out = true,
  };

  const augusta::presentation::ShotJudgement converted = ToPresentation(judgement);

  EXPECT_EQ(converted.shot_tick, 120U);
  EXPECT_FLOAT_EQ(converted.shooters_delay, 6.5F);
  EXPECT_EQ(converted.struck, augusta::presentation::Struck::kHitbox);
  EXPECT_EQ(converted.part, BodyPart::kHead);
  EXPECT_EQ(converted.point, Vec3(1.0F, 1.7F, -4.0F));
  EXPECT_EQ(converted.target, augusta::presentation::EntityId{2});
  ASSERT_EQ(converted.entries.size(), 1U);
  ASSERT_EQ(converted.entries[0].players.size(), 1U);
  const augusta::presentation::JudgedPlayer& player = converted.entries[0].players[0];
  EXPECT_EQ(player.entity, augusta::presentation::EntityId{2});
  EXPECT_EQ(player.pose.position, Vec3(1.0F, 0.0F, -4.0F));
  EXPECT_EQ(player.pose.stance, Stance::kCrouching);
  EXPECT_FLOAT_EQ(player.pose.yaw, 0.25F);
  EXPECT_TRUE(converted.entries_left_out);
}

TEST(FrameMappingTest, EachWayARoundCanEndKeepsItsName) {
  using augusta::link::Struck;
  namespace presentation = augusta::presentation;
  for (const auto& [from, to] :
       {std::pair{Struck::kNothing, presentation::Struck::kNothing},
        std::pair{Struck::kMap, presentation::Struck::kMap}, std::pair{Struck::kProp, presentation::Struck::kProp},
        std::pair{Struck::kHitbox, presentation::Struck::kHitbox}}) {
    augusta::harness::ShotJudgement judgement;
    judgement.struck = from;
    EXPECT_EQ(ToPresentation(judgement).struck, to);
  }
}

}  // namespace
