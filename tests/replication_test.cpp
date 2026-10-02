#include "augusta/replication.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// What each recipient is sent is a pure function of the tick's state.
namespace {

using augusta::replication::PlanHitConfirmations;
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

TEST(ReplicationTest, EachRecipientIsToldItsOwnRifleAndNoOneElses) {
  EntityState firing = PlayerAt(1, 0.0F);
  firing.rifle = {.cooldown = 0.05F, .reload_remaining = 0.0F, .rounds = 12};
  EntityState reloading = PlayerAt(2, 0.0F);
  reloading.rifle = {.cooldown = 0.0F, .reload_remaining = 1.5F, .rounds = 3};
  const State state = StateOf({firing, reloading});
  // Not in the order of the bodies: a recipient finds its own by its entity.
  const std::array<Recipient, 2> recipients = {Recipient{.entity = static_cast<EntityId>(2)},
                                               Recipient{.entity = static_cast<EntityId>(1)}};

  const auto updates = PlanUpdates(state, 1, recipients);

  EXPECT_EQ(updates[0].rifle, reloading.rifle);
  EXPECT_EQ(updates[1].rifle, firing.rifle);
}

TEST(ReplicationTest, EachRecipientIsToldItsOwnHealthAndADeadOneZero) {
  EntityState hurt = PlayerAt(1, 0.0F);
  hurt.health = 30.0F;
  EntityState unhurt = PlayerAt(2, 0.0F);
  unhurt.health = 100.0F;
  // Entity 3 is dead: it has no body in the state.
  const State state = StateOf({hurt, unhurt});
  const std::array<Recipient, 3> recipients = {Recipient{.entity = static_cast<EntityId>(2)},
                                               Recipient{.entity = static_cast<EntityId>(1)},
                                               Recipient{.entity = static_cast<EntityId>(3)}};

  const auto updates = PlanUpdates(state, 1, recipients);

  EXPECT_EQ(updates[0].health, 100.0F);
  EXPECT_EQ(updates[1].health, 30.0F);
  EXPECT_EQ(updates[2].health, 0.0F);
  EXPECT_EQ(updates[2].bodies.size(), 2U);
}

TEST(ReplicationTest, EveryDeathOfATickIsPlannedOnceForEveryone) {
  State state = StateOf({PlayerAt(1, 0.0F)});
  state.deaths = {augusta::simulation::Death{.victim = static_cast<EntityId>(2),
                                             .killer = static_cast<EntityId>(1),
                                             .yaw = 0.5F,
                                             .pitch = -0.25F,
                                             .part = augusta::ballistics::BodyPart::kHead},
                  augusta::simulation::Death{.victim = static_cast<EntityId>(3),
                                             .killer = static_cast<EntityId>(2),
                                             .yaw = 1.0F,
                                             .pitch = 0.0F,
                                             .part = augusta::ballistics::BodyPart::kLimb}};

  const auto deaths = augusta::replication::PlanDeaths(state);

  ASSERT_EQ(deaths.size(), 2U);
  EXPECT_EQ(deaths[0].victim, static_cast<EntityId>(2));
  EXPECT_EQ(deaths[0].killer, static_cast<EntityId>(1));
  EXPECT_EQ(deaths[0].yaw, 0.5F);
  EXPECT_EQ(deaths[0].pitch, -0.25F);
  EXPECT_EQ(deaths[0].part, augusta::ballistics::BodyPart::kHead);
  EXPECT_EQ(deaths[1].victim, static_cast<EntityId>(3));
  EXPECT_EQ(deaths[1].part, augusta::ballistics::BodyPart::kLimb);
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

TEST(ReplicationTest, EveryRecipientIsToldWhereEachBodyFaces) {
  EntityState turned = PlayerAt(1, 0.0F);
  turned.yaw = 1.25F;
  const State state = StateOf({turned, PlayerAt(2, 0.0F)});
  const std::array<Recipient, 1> recipients = {Recipient{.entity = static_cast<EntityId>(2)}};

  const auto updates = PlanUpdates(state, 1, recipients);

  ASSERT_EQ(updates[0].bodies.size(), 2U);
  EXPECT_EQ(updates[0].bodies[0].yaw, 1.25F);
  EXPECT_EQ(updates[0].bodies[1].yaw, 0.0F);
}

TEST(ReplicationTest, EachHitIsConfirmedToItsShooterAloneWithItsTargetBodyPartAndDamage) {
  State state;
  state.hits = {{.shooter = static_cast<EntityId>(1),
                 .target = static_cast<EntityId>(2),
                 .damage = 50.0F,
                 .health = 50.0F,
                 .part = augusta::ballistics::BodyPart::kHead},
                {.shooter = static_cast<EntityId>(3),
                 .target = static_cast<EntityId>(1),
                 .damage = 10.0F,
                 .health = 0.0F,
                 .part = augusta::ballistics::BodyPart::kLimb,
                 .reached_zero = true}};

  const auto confirmations = PlanHitConfirmations(state);

  ASSERT_EQ(confirmations.size(), 2U);
  EXPECT_EQ(confirmations[0].recipient, static_cast<EntityId>(1));
  EXPECT_EQ(confirmations[0].target, static_cast<EntityId>(2));
  EXPECT_EQ(confirmations[0].part, augusta::ballistics::BodyPart::kHead);
  EXPECT_EQ(confirmations[0].damage, 50.0F);
  EXPECT_EQ(confirmations[1].recipient, static_cast<EntityId>(3));
  EXPECT_EQ(confirmations[1].target, static_cast<EntityId>(1));
  EXPECT_EQ(confirmations[1].part, augusta::ballistics::BodyPart::kLimb);
  EXPECT_EQ(confirmations[1].damage, 10.0F);
}

TEST(ReplicationTest, ATickWithoutAHitHasNoHitConfirmationToPlan) {
  EXPECT_TRUE(PlanHitConfirmations(StateOf({PlayerAt(1, 0.0F)})).empty());
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
