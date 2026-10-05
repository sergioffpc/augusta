#include <algorithm>
#include <cstddef>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/protocol.h"

// A match recording's records (ADR-0048), in the protocol's own encoding: pure
// bytes in, record or error out, as the messages are (protocol_test.cpp).
namespace {

using augusta::math::Vec3;
using augusta::protocol::BodyPartWire;
using augusta::protocol::BytesWire;
using augusta::protocol::CommandWire;
using augusta::protocol::DeathWire;
using augusta::protocol::DecodeError;
using augusta::protocol::DecodeRecord;
using augusta::protocol::Encode;
using augusta::protocol::EncodeRecord;
using augusta::protocol::EntityIdWire;
using augusta::protocol::EntityStateWire;
using augusta::protocol::kMaxPlayers;
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
  const auto decoded = DecodeRecord(EncodeRecord(record));
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
                                      .character = "characters/player"}},
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
  const BytesWire record = EncodeRecord(tick);
  const BytesWire message = Encode(augusta::protocol::CommandsWire{
      .commands = {{.sequence = 0, .command = tick.commands.front().command}}, .seen_tick = 0});
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
  BytesWire bytes = EncodeRecord(BusyTick());
  bytes.pop_back();
  EXPECT_EQ(DecodeRecord(bytes).error(), DecodeError::kTruncated);
}

TEST(ProtocolRecordingTest, BytesAfterARecordAreRefused) {
  BytesWire bytes = EncodeRecord(BusyTick());
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
  BytesWire bytes = EncodeRecord(tick);
  // An idle tick ends with its flags.
  bytes.back() = std::byte{0x04};
  EXPECT_EQ(DecodeRecord(bytes).error(), DecodeError::kInvalidEnum);
}

TEST(ProtocolRecordingTest, MoreBodiesThanPlayersAreRefused) {
  RecordedTickWire tick = BusyTick();
  tick.removed.clear();
  tick.match_start.clear();
  tick.commands.clear();
  BytesWire bytes = EncodeRecord(tick);
  // After its type, a tick with nothing removed, started or commanded lists its bodies.
  constexpr std::size_t kBodyCount = 1 + 1 + 1 + 1;
  bytes[kBodyCount] = static_cast<std::byte>(kMaxPlayers + 1);
  EXPECT_EQ(DecodeRecord(bytes).error(), DecodeError::kFieldTooLong);
}

}  // namespace
