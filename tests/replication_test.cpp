#include "augusta/replication.h"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

// What each recipient is sent is a pure function of the tick's state.
namespace {

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

TEST(ReplicationTest, EveryRecipientGetsEveryPlayer) {
  const State state{.bodies = {PlayerAt(1, 10.0F), PlayerAt(2, 20.0F)}};
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
  const State state{.bodies = {PlayerAt(1, 0.0F), PlayerAt(2, 0.0F)}};
  const std::array<Recipient, 2> recipients = {
      Recipient{.entity = static_cast<EntityId>(1), .acknowledged_sequence = 100},
      Recipient{.entity = static_cast<EntityId>(2), .acknowledged_sequence = 7}};

  const auto updates = PlanUpdates(state, 1, recipients);

  EXPECT_EQ(updates[0].recipient, static_cast<EntityId>(1));
  EXPECT_EQ(updates[0].acknowledged_sequence, 100U);
  EXPECT_EQ(updates[1].recipient, static_cast<EntityId>(2));
  EXPECT_EQ(updates[1].acknowledged_sequence, 7U);
}

TEST(ReplicationTest, NobodyToSendToMeansNothingIsPlanned) {
  const State state{.bodies = {PlayerAt(1, 0.0F)}};

  EXPECT_TRUE(PlanUpdates(state, 1, {}).empty());
}

TEST(ReplicationTest, ARecipientWithNoBodyYetStillSeesTheOthers) {
  const State state{.bodies = {PlayerAt(1, 5.0F)}};
  const std::array<Recipient, 1> recipients = {Recipient{.entity = static_cast<EntityId>(2)}};

  const auto updates = PlanUpdates(state, 1, recipients);

  ASSERT_EQ(updates.size(), 1U);
  ASSERT_EQ(updates[0].bodies.size(), 1U);
  EXPECT_EQ(updates[0].bodies[0].entity, static_cast<EntityId>(1));
}

}  // namespace
