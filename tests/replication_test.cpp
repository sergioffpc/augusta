#include "augusta/replication.h"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

// What each recipient is sent is a pure function of the tick's state.
namespace {

using augusta::replication::PlanUpdates;
using augusta::replication::Recipient;
using augusta::simulation::PlayerId;
using augusta::simulation::PlayerState;
using augusta::simulation::State;

PlayerState PlayerAt(std::uint32_t id, float x) {
  PlayerState player{.player = static_cast<PlayerId>(id)};
  player.body.position = augusta::math::Vec3(x, 0.0F, 0.0F);
  return player;
}

TEST(ReplicationTest, EveryRecipientGetsEveryPlayer) {
  const State state{.players = {PlayerAt(1, 10.0F), PlayerAt(2, 20.0F)}};
  const std::array<Recipient, 2> recipients = {Recipient{.player = static_cast<PlayerId>(1)},
                                               Recipient{.player = static_cast<PlayerId>(2)}};

  const auto updates = PlanUpdates(state, 42, recipients);

  ASSERT_EQ(updates.size(), 2U);
  for (const auto& update : updates) {
    EXPECT_EQ(update.tick, 42U);
    ASSERT_EQ(update.players.size(), 2U);
    EXPECT_EQ(update.players[0].player, static_cast<PlayerId>(1));
    EXPECT_EQ(update.players[0].body.position.x, 10.0F);
    EXPECT_EQ(update.players[1].player, static_cast<PlayerId>(2));
    EXPECT_EQ(update.players[1].body.position.x, 20.0F);
  }
}

TEST(ReplicationTest, EachRecipientGetsItsOwnAcknowledgedSequence) {
  const State state{.players = {PlayerAt(1, 0.0F), PlayerAt(2, 0.0F)}};
  const std::array<Recipient, 2> recipients = {
      Recipient{.player = static_cast<PlayerId>(1), .acknowledged_sequence = 100},
      Recipient{.player = static_cast<PlayerId>(2), .acknowledged_sequence = 7}};

  const auto updates = PlanUpdates(state, 1, recipients);

  EXPECT_EQ(updates[0].recipient, static_cast<PlayerId>(1));
  EXPECT_EQ(updates[0].acknowledged_sequence, 100U);
  EXPECT_EQ(updates[1].recipient, static_cast<PlayerId>(2));
  EXPECT_EQ(updates[1].acknowledged_sequence, 7U);
}

TEST(ReplicationTest, NobodyToSendToMeansNothingIsPlanned) {
  const State state{.players = {PlayerAt(1, 0.0F)}};

  EXPECT_TRUE(PlanUpdates(state, 1, {}).empty());
}

TEST(ReplicationTest, ARecipientWithNoBodyYetStillSeesTheOthers) {
  const State state{.players = {PlayerAt(1, 5.0F)}};
  const std::array<Recipient, 1> recipients = {Recipient{.player = static_cast<PlayerId>(2)}};

  const auto updates = PlanUpdates(state, 1, recipients);

  ASSERT_EQ(updates.size(), 1U);
  ASSERT_EQ(updates[0].players.size(), 1U);
  EXPECT_EQ(updates[0].players[0].player, static_cast<PlayerId>(1));
}

}  // namespace
