#include "augusta/replication.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// What each recipient is sent is a pure function of the tick's state.
namespace {

using augusta::replication::PlanShots;
using augusta::replication::PlanUpdates;
using augusta::replication::Recipient;
using augusta::simulation::EntityId;
using augusta::simulation::EntityState;
using augusta::simulation::State;

EntityState PlayerAt(std::uint32_t id, float x) {
  EntityState player{.entity = static_cast<EntityId>(id)};
  player.body.position = augusta::math::Vec3(x, 0.0F, 0.0F);
  return player;
}

// A tick's state holding bodies and nothing else.
State StateOf(std::vector<EntityState> bodies) {
  State state;
  state.bodies = std::move(bodies);
  return state;
}

TEST(ReplicationTest, EveryRecipientGetsEveryPlayer) {
  const State state = StateOf({PlayerAt(1, 10.0F), PlayerAt(2, 20.0F)});
  const std::array<Recipient, 2> recipients = {Recipient{.entity = static_cast<EntityId>(1)},
                                               Recipient{.entity = static_cast<EntityId>(2)}};

  const auto updates = PlanUpdates(state, 42, recipients);

  ASSERT_EQ(updates.size(), 2U);
  for (const auto& update : updates) {
    EXPECT_EQ(update.tick, 42U);
    ASSERT_EQ(update.bodies.size(), 2U);
    EXPECT_EQ(update.bodies[0].entity, static_cast<EntityId>(1));
    EXPECT_EQ(update.bodies[0].body.position.x, 10.0F);
    EXPECT_EQ(update.bodies[1].entity, static_cast<EntityId>(2));
    EXPECT_EQ(update.bodies[1].body.position.x, 20.0F);
  }
}

TEST(ReplicationTest, EachRecipientGetsItsOwnAcknowledgedSequence) {
  const State state = StateOf({PlayerAt(1, 0.0F), PlayerAt(2, 0.0F)});
  const std::array<Recipient, 2> recipients = {
      Recipient{.entity = static_cast<EntityId>(1), .acknowledged_sequence = 100},
      Recipient{.entity = static_cast<EntityId>(2), .acknowledged_sequence = 7}};

  const auto updates = PlanUpdates(state, 1, recipients);

  EXPECT_EQ(updates[0].recipient, static_cast<EntityId>(1));
  EXPECT_EQ(updates[0].acknowledged_sequence, 100U);
  EXPECT_EQ(updates[1].recipient, static_cast<EntityId>(2));
  EXPECT_EQ(updates[1].acknowledged_sequence, 7U);
}

TEST(ReplicationTest, EachRecipientIsToldHowManyOfItsOwnCommandsAreQueued) {
  const State state = StateOf({PlayerAt(1, 0.0F), PlayerAt(2, 0.0F)});
  const std::array<Recipient, 2> recipients = {Recipient{.entity = static_cast<EntityId>(1), .queued_commands = 3},
                                               Recipient{.entity = static_cast<EntityId>(2), .queued_commands = 0}};

  const auto updates = PlanUpdates(state, 1, recipients);

  EXPECT_EQ(updates[0].queued_commands, 3U);
  EXPECT_EQ(updates[1].queued_commands, 0U);
}

TEST(ReplicationTest, NobodyToSendToMeansNothingIsPlanned) {
  const State state = StateOf({PlayerAt(1, 0.0F)});

  EXPECT_TRUE(PlanUpdates(state, 1, {}).empty());
}

TEST(ReplicationTest, EveryShotOfATickIsPlannedOnceUnderThatTick) {
  State state;
  state.shots = {{.shooter = static_cast<EntityId>(1),
                  .origin = augusta::math::Vec3(1.0F, 2.0F, 3.0F),
                  .yaw = 0.5F,
                  .pitch = -0.25F},
                 {.shooter = static_cast<EntityId>(2)}};

  const auto shots = PlanShots(state, 42);

  ASSERT_EQ(shots.size(), 2U);
  EXPECT_EQ(shots[0].shooter, static_cast<EntityId>(1));
  EXPECT_EQ(shots[0].tick, 42U);
  EXPECT_EQ(shots[0].origin, augusta::math::Vec3(1.0F, 2.0F, 3.0F));
  EXPECT_EQ(shots[0].yaw, 0.5F);
  EXPECT_EQ(shots[0].pitch, -0.25F);
  EXPECT_EQ(shots[1].shooter, static_cast<EntityId>(2));
  EXPECT_EQ(shots[1].tick, 42U);
}

TEST(ReplicationTest, ATickWithoutFireHasNoShotToPlan) {
  EXPECT_TRUE(PlanShots(StateOf({PlayerAt(1, 0.0F)}), 1).empty());
}

TEST(ReplicationTest, ARecipientWithNoBodyYetStillSeesTheOthers) {
  const State state = StateOf({PlayerAt(1, 5.0F)});
  const std::array<Recipient, 1> recipients = {Recipient{.entity = static_cast<EntityId>(2)}};

  const auto updates = PlanUpdates(state, 1, recipients);

  ASSERT_EQ(updates.size(), 1U);
  ASSERT_EQ(updates[0].bodies.size(), 1U);
  EXPECT_EQ(updates[0].bodies[0].entity, static_cast<EntityId>(1));
}

}  // namespace
