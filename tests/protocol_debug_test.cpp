#include <cstddef>
#include <cstdint>
#include <limits>
#include <variant>

#include <gtest/gtest.h>

#include "augusta/grid.h"
#include "augusta/limits.h"
#include "augusta/math.h"
#include "augusta/protocol.h"
#include "protobuf_bytes.h"

// The messages of a Development build (ADR-0038, ADR-0074), which a Shipping
// build's codec does not have: built only into a Development build's protocol
// tests.
namespace {

using augusta::limits::kMaxShotJudgementEntries;
using augusta::math::Vec3;
using augusta::protobuf_bytes::Cat;
using augusta::protobuf_bytes::LengthField;
using augusta::protobuf_bytes::SignedField;
using augusta::protobuf_bytes::VarintField;
using augusta::protocol::BodyPartWire;
using augusta::protocol::BytesWire;
using augusta::protocol::DebugSubscribeWire;
using augusta::protocol::Decode;
using augusta::protocol::DecodeError;
using augusta::protocol::Encode;
using augusta::protocol::EncodeError;
using augusta::protocol::EntityIdWire;
using augusta::protocol::JudgedPlayerWire;
using augusta::protocol::MessageTypeWire;
using augusta::protocol::MessageWire;
using augusta::protocol::ShotJudgementEntryWire;
using augusta::protocol::ShotJudgementWire;
using augusta::protocol::StanceWire;
using augusta::protocol::StruckWire;

// The payload of a message of type whose body holds fields.
BytesWire Envelope(MessageTypeWire type, const BytesWire& fields) {
  return LengthField(static_cast<int>(type), fields);
}

// message as the other peer decodes it.
MessageWire RoundTrip(const MessageWire& message) { return Decode(Encode(message).value()).value(); }

// A tick of a flight that crossed players players' bounds, each a body of its
// own on the grids.
ShotJudgementEntryWire EntryCrossing(std::uint16_t ticks_after_shot, std::size_t players) {
  ShotJudgementEntryWire entry{.players = {}, .ticks_after_shot = ticks_after_shot};
  for (std::size_t i = 0; i < players; ++i) {
    const auto step = static_cast<float>(i);
    entry.players.push_back(JudgedPlayerWire{.position = Vec3(1.0F + step, 0.5F, -40.25F),
                                             .entity = static_cast<EntityIdWire>(i + 2),
                                             .yaw = -0.5F + (step / 1024.0F),
                                             .stance = static_cast<StanceWire>(i % 3)});
  }
  return entry;
}

// A round that struck a Hitbox's head, after a delay of 6 ticks and a half.
ShotJudgementWire AHeadshot() {
  return ShotJudgementWire{.shot_tick = 1200,
                           .entries = {},
                           .point = Vec3(3.0F, 1.75F, -40.0F),
                           .target = static_cast<EntityIdWire>(7),
                           .delay_fraction = 0.5F,
                           .delay_ticks = 6,
                           .struck = StruckWire::kHitbox,
                           .part = BodyPartWire::kHead,
                           .flags = 0};
}

// Requirements: US-30
TEST(ProtocolDebugTest, ADebugSubscribeIsTheEnvelopesEighteenthMember) {
  EXPECT_EQ(static_cast<int>(MessageTypeWire::kDebugSubscribe), 18);
  EXPECT_EQ(augusta::protocol::TypeOf(MessageWire{DebugSubscribeWire{}}), MessageTypeWire::kDebugSubscribe);
}

// Every bit of the byte travels, a bit no stream uses yet included: the server
// ignores those, so a later stream's bit is never a malformed message.
// Requirements: US-30
TEST(ProtocolDebugTest, ADebugSubscribeRoundTripsAnyByteOfFlags) {
  for (const std::uint8_t flags :
       {std::uint8_t{0}, DebugSubscribeWire::kShotJudgements, std::uint8_t{0x80}, std::uint8_t{0xFF}}) {
    const auto decoded = Decode(Encode(DebugSubscribeWire{.flags = flags}).value());

    ASSERT_TRUE(decoded.has_value()) << +flags;
    ASSERT_TRUE(std::holds_alternative<DebugSubscribeWire>(*decoded)) << +flags;
    EXPECT_EQ(std::get<DebugSubscribeWire>(*decoded).flags, flags);
  }
}

// Requirements: US-30
TEST(ProtocolDebugTest, ADebugSubscribesFlagsAreItsFirstField) {
  EXPECT_EQ(Encode(DebugSubscribeWire{.flags = DebugSubscribeWire::kShotJudgements}).value(),
            Envelope(MessageTypeWire::kDebugSubscribe, VarintField(1, 1)));
  EXPECT_EQ(Encode(DebugSubscribeWire{}).value(), Envelope(MessageTypeWire::kDebugSubscribe, {}));
}

// Requirements: NFR-12
TEST(ProtocolDebugTest, ADebugSubscribesFlagsWiderThanAByteAreOutOfRange) {
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kDebugSubscribe, VarintField(1, 0x100))).error(),
            DecodeError::kOutOfRange);
}

// Requirements: NFR-12
TEST(ProtocolDebugTest, EveryTruncationOfADebugSubscribeIsMalformedNotACrash) {
  const BytesWire whole = Encode(DebugSubscribeWire{.flags = 0xFF}).value();
  for (std::size_t length = 1; length < whole.size(); ++length) {
    const BytesWire cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(length));
    EXPECT_EQ(Decode(cut).error(), DecodeError::kMalformed) << "cut to " << length;
  }
}

// Requirements: US-30
TEST(ProtocolDebugTest, AShotJudgementIsTheEnvelopesNineteenthMember) {
  EXPECT_EQ(static_cast<int>(MessageTypeWire::kShotJudgement), 19);
  EXPECT_EQ(augusta::protocol::TypeOf(MessageWire{ShotJudgementWire{}}), MessageTypeWire::kShotJudgement);
}

// Requirements: US-30
TEST(ProtocolDebugTest, AShotJudgementWithNoEntriesRoundTrips) {
  for (const StruckWire struck : {StruckWire::kNothing, StruckWire::kMap, StruckWire::kProp, StruckWire::kHitbox}) {
    ShotJudgementWire judgement = AHeadshot();
    judgement.struck = struck;

    EXPECT_EQ(RoundTrip(judgement), MessageWire{judgement}) << static_cast<int>(struck);
  }
}

// Requirements: US-30
TEST(ProtocolDebugTest, AShotJudgementWithEveryEntryItHoldsRoundTrips) {
  ShotJudgementWire judgement = AHeadshot();
  for (std::size_t i = 0; i < kMaxShotJudgementEntries; ++i) {
    judgement.entries.push_back(EntryCrossing(static_cast<std::uint16_t>(i * 4096), 1 + (i % 3)));
  }
  judgement.entries.back().ticks_after_shot = std::numeric_limits<std::uint16_t>::max();
  judgement.flags = ShotJudgementWire::kEntriesLeftOut;

  EXPECT_EQ(RoundTrip(judgement), MessageWire{judgement});
}

// The bitfield ADR-0038 gives it: what it struck in bits 0-1, the Body part
// in bits 2-3, the entries left out in bit 4.
// Requirements: US-30
TEST(ProtocolDebugTest, AShotJudgementsDelayStruckAndPartTravelAsADR0038Says) {
  ShotJudgementWire judgement = AHeadshot();
  judgement.flags = ShotJudgementWire::kEntriesLeftOut;
  judgement.entries = {
      ShotJudgementEntryWire{.players = {JudgedPlayerWire{.position = Vec3(1.0F, 0.0F, -1.0F / 1024.0F),
                                                          .entity = static_cast<EntityIdWire>(9),
                                                          .yaw = 1.0F,
                                                          .stance = StanceWire::kProne}},
                             .ticks_after_shot = 300}};

  const BytesWire player =
      Cat({VarintField(1, 9), SignedField(2, 1024), SignedField(4, -1), VarintField(5, 2), SignedField(6, 2097152)});
  const BytesWire entry = Cat({VarintField(1, 300), LengthField(2, player)});
  const BytesWire fields = Cat(
      {VarintField(1, 1200), VarintField(2, 6), VarintField(3, 128), VarintField(4, 3U | (1U << 2U) | (1U << 4U)),
       SignedField(5, 3072), SignedField(6, 1792), SignedField(7, -40960), VarintField(8, 7), LengthField(9, entry)});
  EXPECT_EQ(Encode(judgement).value(), Envelope(MessageTypeWire::kShotJudgement, fields));
}

// Requirements: NFR-12
TEST(ProtocolDebugTest, AShotJudgementsBitsTheProtocolLacksAreRefused) {
  // A Body part of 0, bit 5 and a stance of 3.
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kShotJudgement, VarintField(4, 3))).error(), DecodeError::kInvalidEnum);
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kShotJudgement, VarintField(4, (2U << 2U) | (1U << 5U)))).error(),
            DecodeError::kReservedBits);
  const BytesWire bad_stance = LengthField(9, LengthField(2, VarintField(5, 3)));
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kShotJudgement, Cat({VarintField(4, 2U << 2U), bad_stance}))).error(),
            DecodeError::kInvalidEnum);
}

// Requirements: NFR-12
TEST(ProtocolDebugTest, AShotJudgementsNumbersWiderThanTheirFieldsAreOutOfRange) {
  const BytesWire part = VarintField(4, 2U << 2U);
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kShotJudgement, Cat({part, VarintField(2, 256)}))).error(),
            DecodeError::kOutOfRange);
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kShotJudgement, Cat({part, VarintField(3, 256)}))).error(),
            DecodeError::kOutOfRange);
  EXPECT_EQ(
      Decode(Envelope(MessageTypeWire::kShotJudgement, Cat({part, LengthField(9, VarintField(1, 65536))}))).error(),
      DecodeError::kOutOfRange);
}

// Requirements: NFR-12
TEST(ProtocolDebugTest, AShotJudgementWithMoreEntriesOrPlayersThanItHoldsIsRefused) {
  ShotJudgementWire too_many_entries = AHeadshot();
  too_many_entries.entries.assign(kMaxShotJudgementEntries + 1, EntryCrossing(0, 1));
  ShotJudgementWire too_many_players = AHeadshot();
  too_many_players.entries = {EntryCrossing(0, augusta::limits::kMaxPlayers + 1)};

  EXPECT_EQ(Encode(too_many_entries).error(), EncodeError::kFieldTooLong);
  EXPECT_EQ(Encode(too_many_players).error(), EncodeError::kFieldTooLong);
  const BytesWire entry = LengthField(9, VarintField(1, 1));
  BytesWire seventeen = VarintField(4, 2U << 2U);
  for (std::size_t i = 0; i <= kMaxShotJudgementEntries; ++i) {
    seventeen = Cat({seventeen, entry});
  }
  EXPECT_EQ(Decode(Envelope(MessageTypeWire::kShotJudgement, seventeen)).error(), DecodeError::kFieldTooLong);
}

// Requirements: US-30
TEST(ProtocolDebugTest, AShotJudgementsFlagsAndEnumerationsTheProtocolLacksAreNotEncoded) {
  ShotJudgementWire flags = AHeadshot();
  flags.flags = ShotJudgementWire::kEntriesLeftOut << 1U;
  ShotJudgementWire struck = AHeadshot();
  struck.struck = static_cast<StruckWire>(4);
  ShotJudgementWire part = AHeadshot();
  part.part = static_cast<BodyPartWire>(0);
  ShotJudgementWire stance = AHeadshot();
  stance.entries = {EntryCrossing(0, 1)};
  stance.entries.front().players.front().stance = static_cast<StanceWire>(3);

  EXPECT_EQ(Encode(flags).error(), EncodeError::kReservedBits);
  EXPECT_EQ(Encode(struck).error(), EncodeError::kInvalidEnum);
  EXPECT_EQ(Encode(part).error(), EncodeError::kInvalidEnum);
  EXPECT_EQ(Encode(stance).error(), EncodeError::kInvalidEnum);
}

// Requirements: NFR-12
TEST(ProtocolDebugTest, EveryTruncationOfAShotJudgementIsMalformedNotACrash) {
  ShotJudgementWire judgement = AHeadshot();
  judgement.entries = {EntryCrossing(1, 2), EntryCrossing(2, 1)};
  const BytesWire whole = Encode(judgement).value();
  for (std::size_t length = 1; length < whole.size(); ++length) {
    const BytesWire cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(length));
    EXPECT_EQ(Decode(cut).error(), DecodeError::kMalformed) << "cut to " << length;
  }
}

// Its 16 entries, each crossing every player a Match holds, with every number
// at its widest, still fit one payload.
// Requirements: US-30
TEST(ProtocolDebugTest, TheLargestShotJudgementFitsTheProtocolsMessageSizeLimit) {
  const augusta::math::Grid& position = augusta::math::kPositionGrid;
  const float far = augusta::math::FromSteps(position.min, position);
  JudgedPlayerWire widest{.position = Vec3(far, far, far),
                          .entity = static_cast<EntityIdWire>(std::numeric_limits<std::uint32_t>::max()),
                          .yaw = augusta::math::FromSteps(augusta::math::kAngleGrid.min, augusta::math::kAngleGrid),
                          .stance = StanceWire::kProne};
  ShotJudgementWire largest{
      .shot_tick = std::numeric_limits<std::uint64_t>::max(),
      .entries = {},
      .point = Vec3(far, far, far),
      .target = static_cast<EntityIdWire>(std::numeric_limits<std::uint32_t>::max()),
      .delay_fraction = augusta::math::FromSteps(augusta::math::kFractionGrid.max, augusta::math::kFractionGrid),
      .delay_ticks = std::numeric_limits<std::uint8_t>::max(),
      .struck = StruckWire::kHitbox,
      .part = BodyPartWire::kLimb,
      .flags = ShotJudgementWire::kEntriesLeftOut};
  largest.entries.assign(
      kMaxShotJudgementEntries,
      ShotJudgementEntryWire{.players = {}, .ticks_after_shot = std::numeric_limits<std::uint16_t>::max()});
  for (ShotJudgementEntryWire& entry : largest.entries) {
    entry.players.assign(augusta::limits::kMaxPlayers, widest);
  }

  const auto encoded = Encode(largest);

  ASSERT_TRUE(encoded.has_value());
  EXPECT_LE(encoded->size(), augusta::protocol::kMaxPayloadSize);
}

}  // namespace
