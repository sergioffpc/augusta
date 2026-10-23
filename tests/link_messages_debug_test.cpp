#include <cstdint>
#include <optional>
#include <variant>

#include <gtest/gtest.h>

#include "augusta/body_part.h"
#include "augusta/link_messages.h"
#include "augusta/link_types.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"

// The Link's conversions of a Development build's messages (ADR-0038,
// ADR-0074), as link_messages_test.cpp has the rest's.
namespace {

using augusta::link::DebugSubscribe;
using augusta::link::EntityId;
using augusta::link::JudgedPlayer;
using augusta::link::ShotJudgement;
using augusta::link::ShotJudgementEntry;
using augusta::link::Struck;
using augusta::link::ToWire;
using augusta::math::Vec3;

// message as the other peer decodes it.
augusta::protocol::MessageWire ThroughTheWire(const augusta::protocol::MessageWire& message) {
  return augusta::protocol::Decode(augusta::protocol::Encode(message).value()).value();
}

// judgement as the shooter's client receives it.
ShotJudgement Received(const ShotJudgement& judgement) {
  const auto received = augusta::link::ServerMessageFromWire(ThroughTheWire(ToWire(judgement)));
  EXPECT_TRUE(received.has_value());
  EXPECT_TRUE(received.has_value() && std::holds_alternative<ShotJudgement>(*received));
  return received.has_value() && std::holds_alternative<ShotJudgement>(*received) ? std::get<ShotJudgement>(*received)
                                                                                  : ShotJudgement{};
}

// A round that struck body 7's head after a Shooter's delay of 6 ticks and a half.
ShotJudgement AHeadshot() {
  return ShotJudgement{.shot_tick = 1200,
                       .shooters_delay = 6.5F,
                       .struck = Struck::kHitbox,
                       .part = augusta::body_part::BodyPart::kHead,
                       .point = Vec3(3.0F, 1.75F, -40.0F),
                       .target = EntityId{7},
                       .entries = {},
                       .entries_left_out = false};
}

// A tick whose segment crossed bodies 2 and 3.
ShotJudgementEntry CrossingTwo(std::uint16_t ticks_after_shot) {
  return ShotJudgementEntry{.ticks_after_shot = ticks_after_shot,
                            .players = {JudgedPlayer{.entity = EntityId{2},
                                                     .position = Vec3(3.0F, 0.0F, -40.0F),
                                                     .stance = augusta::physics::Stance::kCrouching,
                                                     .yaw = 1.5F},
                                        JudgedPlayer{.entity = EntityId{3},
                                                     .position = Vec3(-1.25F, 0.0F, -43.0F),
                                                     .stance = augusta::physics::Stance::kProne,
                                                     .yaw = -0.75F}}};
}

TEST(LinkMessagesDebugTest, ADebugSubscribeTheClientSendsReachesTheServerWithItsFlags) {
  for (const std::uint8_t flags : {std::uint8_t{0}, DebugSubscribe::kShotJudgements, std::uint8_t{0xFF}}) {
    const auto received = augusta::link::ClientMessageFromWire(ThroughTheWire(ToWire(DebugSubscribe{.flags = flags})));

    ASSERT_TRUE(received.has_value()) << +flags;
    ASSERT_TRUE(std::holds_alternative<DebugSubscribe>(*received)) << +flags;
    EXPECT_EQ(std::get<DebugSubscribe>(*received).flags, flags);
  }
}

TEST(LinkMessagesDebugTest, ADebugSubscribesShotJudgementsBitIsTheProtocols) {
  EXPECT_EQ(DebugSubscribe::kShotJudgements, augusta::protocol::DebugSubscribeWire::kShotJudgements);
}

TEST(LinkMessagesDebugTest, ADebugSubscribeIsNoServerMessage) {
  EXPECT_FALSE(augusta::link::ServerMessageFromWire(ToWire(DebugSubscribe{})).has_value());
}

TEST(LinkMessagesDebugTest, ADebugSubscribeIsOfItsOwnType) {
  EXPECT_EQ(augusta::link::TypeOf(augusta::link::EncodeToSend(ToWire(DebugSubscribe{})).value()),
            augusta::link::MessageType::kDebugSubscribe);
  EXPECT_EQ(augusta::link::NameOf(augusta::link::MessageType::kDebugSubscribe), "debug_subscribe");
}

TEST(LinkMessagesDebugTest, AShotJudgementWithNoEntriesReachesItsShooterAsTheServerJudgedIt) {
  for (const Struck struck : {Struck::kNothing, Struck::kMap, Struck::kProp, Struck::kHitbox}) {
    ShotJudgement judgement = AHeadshot();
    judgement.struck = struck;

    EXPECT_EQ(Received(judgement), judgement) << static_cast<int>(struck);
  }
}

TEST(LinkMessagesDebugTest, AShotJudgementWithSixteenEntriesAndSomeLeftOutReachesItsShooterWhole) {
  ShotJudgement judgement = AHeadshot();
  for (std::uint16_t i = 0; i < 16; ++i) {
    judgement.entries.push_back(CrossingTwo(i));
  }
  judgement.entries_left_out = true;

  EXPECT_EQ(Received(judgement), judgement);
}

// The entity 0 names no body: what the wire carries for a round that struck none.
TEST(LinkMessagesDebugTest, AShotJudgementThatStruckNoBodyOrPropNamesNoTarget) {
  ShotJudgement judgement = AHeadshot();
  judgement.struck = Struck::kMap;
  judgement.target = std::nullopt;

  EXPECT_EQ(ToWire(judgement).target, augusta::protocol::EntityIdWire{0});
  EXPECT_EQ(Received(judgement).target, std::nullopt);
}

// The whole ticks travel as a byte, the fraction on the Seen fraction grid
// (ADR-0038): a delay beyond either is held to the most they carry.
TEST(LinkMessagesDebugTest, AShotJudgementsShootersDelayTravelsAsWholeTicksAndAFraction) {
  ShotJudgement judgement = AHeadshot();
  judgement.shooters_delay = 18.25F;
  EXPECT_EQ(ToWire(judgement).delay_ticks, 18);
  EXPECT_FLOAT_EQ(ToWire(judgement).delay_fraction, 0.25F);
  EXPECT_FLOAT_EQ(Received(judgement).shooters_delay, 18.25F);

  judgement.shooters_delay = 300.0F;
  EXPECT_EQ(ToWire(judgement).delay_ticks, 255);
  judgement.shooters_delay = -1.0F;
  EXPECT_EQ(ToWire(judgement).delay_ticks, 0);
  EXPECT_FLOAT_EQ(ToWire(judgement).delay_fraction, 0.0F);
}

TEST(LinkMessagesDebugTest, AShotJudgementIsNoClientMessageAndIsOfItsOwnType) {
  EXPECT_FALSE(augusta::link::ClientMessageFromWire(ToWire(AHeadshot())).has_value());
  EXPECT_EQ(augusta::link::TypeOf(augusta::link::EncodeToSend(ToWire(AHeadshot())).value()),
            augusta::link::MessageType::kShotJudgement);
  EXPECT_EQ(augusta::link::NameOf(augusta::link::MessageType::kShotJudgement), "shot_judgement");
  EXPECT_EQ(augusta::link::kLastMessageType, augusta::link::MessageType::kShotJudgement);
}

}  // namespace
