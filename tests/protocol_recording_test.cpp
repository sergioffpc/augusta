#include <algorithm>
#include <cstddef>
#include <string>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/primitives.h"
#include "augusta/protocol.h"

// A match recording's records (ADR-0048), in the protocol's own encoding: pure
// bytes in, record or error out, as the messages are (protocol_test.cpp).
namespace {

using augusta::math::Vec3;
using augusta::primitives::kMaxPlayers;
using augusta::protocol::BodyPartWire;
using augusta::protocol::BytesWire;
using augusta::protocol::CommandWire;
using augusta::protocol::DeathWire;
using augusta::protocol::DecodeError;
using augusta::protocol::DecodeRecord;
using augusta::protocol::Encode;
using augusta::protocol::EncodeError;
using augusta::protocol::EncodeRecord;
using augusta::protocol::EntityIdWire;
using augusta::protocol::EntityStateWire;
using augusta::protocol::kMaxCharacterNameLength;
using augusta::protocol::kMaxEngineVersionLength;
using augusta::protocol::kMaxRecordedHits;
using augusta::protocol::MatchPlayerWire;
using augusta::protocol::PackHashWire;
using augusta::protocol::RecordedBodyWire;
using augusta::protocol::RecordedCommandWire;
using augusta::protocol::RecordedHitWire;
using augusta::protocol::RecordedTickWire;
using augusta::protocol::RecordingHeaderWire;
using augusta::protocol::RecordWire;
using augusta::protocol::SessionIdWire;
using augusta::protocol::ShotWire;
using augusta::protocol::StanceWire;
using augusta::protocol::WeaponStateWire;

RecordWire RoundTrip(const RecordWire& record) {
  const auto decoded = DecodeRecord(EncodeRecord(record).value());
  EXPECT_TRUE(decoded.has_value());
  return decoded.value_or(RecordWire{});
}

PackHashWire CountingPackHash() {
  PackHashWire hash{};
  for (std::size_t i = 0; i < hash.size(); ++i) {
    hash[i] = static_cast<std::byte>(i + 1);
  }
  return hash;
}

// A tick that holds one of everything a tick's record can, every number on its grid.
RecordedTickWire BusyTick() {
  return RecordedTickWire{
      .removed = {EntityIdWire{7}},
      .match_start = {MatchPlayerWire{.spawn = Vec3(1.0F, 0.0F, -2.5F),
                                      .session = SessionIdWire{3},
                                      .entity = EntityIdWire{9},
                                      .character = "soldier"}},
      .commands = {RecordedCommandWire{.seen_tick = 0x1'0000'0002ULL,
                                       .command = CommandWire{.direction = Vec3(0.5F, 0.0F, -1.0F),
                                                              .yaw = 0.25F,
                                                              .pitch = -0.125F,
                                                              .seen_fraction = 0.5F,
                                                              .flags = CommandWire::kFire | CommandWire::kAds,
                                                              .desired_stance = StanceWire::kCrouching,
                                                              .seen_age = 0},
                                       .entity = EntityIdWire{9}}},
      .bodies = {RecordedBodyWire{.state = EntityStateWire{.entity = EntityIdWire{9},
                                                           .body = {.position = Vec3(1.0F, 0.5F, -2.5F),
                                                                    .velocity = Vec3(0.0F, -0.25F, 4.0F),
                                                                    .stamina = 0.75F,
                                                                    .flags = 0,
                                                                    .stance = StanceWire::kProne},
                                                           .yaw = 0.25F},
                                  .rifle = WeaponStateWire{.cooldown = 0.1F,
                                                           .reload_remaining = 0.0F,
                                                           .recoil_pitch = 0.0078125F,
                                                           .recoil_yaw = -0.00390625F,
                                                           .rounds = 29,
                                                           .burst_index = 1},
                                  .health = 73.5F}},
      .shots = {ShotWire{
          .tick = 42, .origin = Vec3(1.0F, 1.5F, -2.5F), .shooter = EntityIdWire{9}, .yaw = 0.25F, .pitch = -0.125F}},
      .hits = {RecordedHitWire{.damage = 26.5F,
                               .health = 0.0F,
                               .shooter = EntityIdWire{9},
                               .target = EntityIdWire{4},
                               .part = BodyPartWire::kHead,
                               .flags = RecordedHitWire::kReachedZero}},
      .deaths = {DeathWire{.victim = EntityIdWire{4},
                           .killer = EntityIdWire{9},
                           .yaw = 0.25F,
                           .pitch = -0.125F,
                           .part = BodyPartWire::kHead}},
      .delta_time = 1.0F / 60.0F,
      .winner = SessionIdWire{3},
      .flags = RecordedTickWire::kMatchEnded | RecordedTickWire::kPolicyMatchEnd,
  };
}

TEST(ProtocolRecordingTest, AHeaderRoundTrips) {
  const RecordingHeaderWire header{.server_pack = CountingPackHash(), .engine_version = "0.4.0", .tick_rate_hz = 60};
  EXPECT_EQ(std::get<RecordingHeaderWire>(RoundTrip(header)), header);
}

TEST(ProtocolRecordingTest, AnIdleTickRoundTrips) {
  const RecordedTickWire tick{.removed = {},
                              .match_start = {},
                              .commands = {},
                              .bodies = {},
                              .shots = {},
                              .hits = {},
                              .deaths = {},
                              .delta_time = 1.0F / 60.0F,
                              .winner = {},
                              .flags = 0};
  EXPECT_EQ(std::get<RecordedTickWire>(RoundTrip(tick)), tick);
}

TEST(ProtocolRecordingTest, ATickWithOneOfEverythingRoundTrips) {
  EXPECT_EQ(std::get<RecordedTickWire>(RoundTrip(BusyTick())), BusyTick());
}

TEST(ProtocolRecordingTest, ATickOfAFullMatchRoundTrips) {
  RecordedTickWire tick = BusyTick();
  const RecordedCommandWire command = tick.commands.front();
  const RecordedBodyWire body = tick.bodies.front();
  tick.commands.assign(kMaxPlayers, command);
  tick.bodies.assign(kMaxPlayers, body);
  EXPECT_EQ(std::get<RecordedTickWire>(RoundTrip(tick)), tick);
}

// A recording carries a command exactly as a Commands message does.
TEST(ProtocolRecordingTest, ARecordedCommandIsEncodedAsACommandsMessageEncodesIt) {
  const RecordedTickWire tick = BusyTick();
  const BytesWire record = EncodeRecord(tick).value();
  const BytesWire message =
      Encode(augusta::protocol::CommandsWire{.commands = {{.sequence = 0, .command = tick.commands.front().command}},
                                             .seen_tick = 0})
          .value();
  // The message's command: after its type, its count and its 8-byte sequence, up to its Seen tick.
  constexpr std::size_t kCommandStart = 1 + 1 + 8;
  constexpr std::size_t kSeenTickSize = 8;
  const BytesWire command(message.begin() + kCommandStart, message.end() - kSeenTickSize);
  EXPECT_NE(std::search(record.begin(), record.end(), command.begin(), command.end()), record.end());
}

TEST(ProtocolRecordingTest, AnEmptyPayloadIsNoRecord) {
  EXPECT_EQ(DecodeRecord(BytesWire{}).error(), DecodeError::kEmpty);
}

TEST(ProtocolRecordingTest, AnUnknownTypeIsNoRecord) {
  EXPECT_EQ(DecodeRecord(BytesWire{std::byte{0}}).error(), DecodeError::kUnknownType);
  EXPECT_EQ(DecodeRecord(BytesWire{std::byte{3}}).error(), DecodeError::kUnknownType);
}

TEST(ProtocolRecordingTest, ATruncatedTickIsRefused) {
  BytesWire bytes = EncodeRecord(BusyTick()).value();
  bytes.pop_back();
  EXPECT_EQ(DecodeRecord(bytes).error(), DecodeError::kTruncated);
}

TEST(ProtocolRecordingTest, BytesAfterARecordAreRefused) {
  BytesWire bytes = EncodeRecord(BusyTick()).value();
  bytes.push_back(std::byte{0});
  EXPECT_EQ(DecodeRecord(bytes).error(), DecodeError::kTrailingBytes);
}

TEST(ProtocolRecordingTest, ATickFlagNoTickHasIsRefused) {
  RecordedTickWire tick = BusyTick();
  tick.removed.clear();
  tick.match_start.clear();
  tick.commands.clear();
  tick.bodies.clear();
  tick.shots.clear();
  tick.hits.clear();
  tick.deaths.clear();
  BytesWire bytes = EncodeRecord(tick).value();
  // An idle tick ends with its flags.
  bytes.back() = std::byte{0x04};
  EXPECT_EQ(DecodeRecord(bytes).error(), DecodeError::kInvalidEnum);
}

TEST(ProtocolRecordingTest, MoreBodiesThanPlayersAreRefused) {
  RecordedTickWire tick = BusyTick();
  tick.removed.clear();
  tick.match_start.clear();
  tick.commands.clear();
  BytesWire bytes = EncodeRecord(tick).value();
  // After its type, a tick with nothing removed, started or commanded lists its bodies.
  constexpr std::size_t kBodyCount = 1 + 1 + 1 + 1;
  bytes[kBodyCount] = static_cast<std::byte>(kMaxPlayers + 1);
  EXPECT_EQ(DecodeRecord(bytes).error(), DecodeError::kFieldTooLong);
}

// EncodeRecord refuses a record that breaks one of the protocol's limits, in
// every build, as Encode does a message: it gives back no payload, so a
// recording never holds a record a reader would refuse (ADR-0033).

// The error EncodeRecord gives record, which must be one it refuses.
EncodeError RefusalOf(const RecordWire& record) {
  const auto encoded = EncodeRecord(record);
  EXPECT_FALSE(encoded.has_value());
  return encoded.has_value() ? EncodeError{} : encoded.error();
}

TEST(ProtocolRecordingEncodeTest, AHeadersEngineVersionLongerThanAllowedIsNotEncoded) {
  const RecordingHeaderWire header{
      .server_pack = {}, .engine_version = std::string(kMaxEngineVersionLength + 1, 'v'), .tick_rate_hz = 60};

  EXPECT_EQ(RefusalOf(header), EncodeError::kFieldTooLong);
}

TEST(ProtocolRecordingEncodeTest, ATickWithAListLongerThanItAllowsIsNotEncoded) {
  RecordedTickWire too_many_removed = BusyTick();
  too_many_removed.removed.assign(kMaxPlayers + 1, EntityIdWire{1});
  RecordedTickWire too_many_hits = BusyTick();
  too_many_hits.hits.assign(kMaxRecordedHits + 1, BusyTick().hits.front());
  RecordedTickWire too_long_a_character = BusyTick();
  too_long_a_character.match_start.front().character.assign(kMaxCharacterNameLength + 1, 'c');

  EXPECT_EQ(RefusalOf(too_many_removed), EncodeError::kFieldTooLong);
  EXPECT_EQ(RefusalOf(too_many_hits), EncodeError::kFieldTooLong);
  EXPECT_EQ(RefusalOf(too_long_a_character), EncodeError::kFieldTooLong);
}

TEST(ProtocolRecordingEncodeTest, AFlagBitATickOrAHitDoesNotHaveIsNotEncoded) {
  RecordedTickWire tick = BusyTick();
  tick.flags = RecordedTickWire::kPolicyMatchEnd << 1U;
  RecordedTickWire hit = BusyTick();
  hit.hits.front().flags = RecordedHitWire::kReachedZero << 1U;
  RecordedTickWire command = BusyTick();
  command.commands.front().command.flags = CommandWire::kReload << 1U;

  EXPECT_EQ(RefusalOf(tick), EncodeError::kReservedBits);
  EXPECT_EQ(RefusalOf(hit), EncodeError::kReservedBits);
  EXPECT_EQ(RefusalOf(command), EncodeError::kReservedBits);
}

TEST(ProtocolRecordingEncodeTest, AnEnumeratedValueTheEnumerationLacksIsNotEncoded) {
  RecordedTickWire hit = BusyTick();
  hit.hits.front().part = static_cast<BodyPartWire>(0);
  RecordedTickWire body = BusyTick();
  body.bodies.front().state.body.stance = static_cast<StanceWire>(3);

  EXPECT_EQ(RefusalOf(hit), EncodeError::kInvalidEnum);
  EXPECT_EQ(RefusalOf(body), EncodeError::kInvalidEnum);
}

TEST(ProtocolRecordingEncodeTest, EveryRecordNamesItsTypeAsItsFirstByteWould) {
  for (const RecordWire& record : {RecordWire{RecordingHeaderWire{}}, RecordWire{BusyTick()}}) {
    EXPECT_EQ(static_cast<std::byte>(augusta::protocol::TypeOf(record)), EncodeRecord(record).value().front());
  }
}

}  // namespace
