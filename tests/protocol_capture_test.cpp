#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "augusta/grid.h"
#include "augusta/math.h"
#include "augusta/protocol.h"

// A Match capture's records (ADR-0050), in the protocol's own encoding: pure
// bytes in, record or error out, as the messages are (protocol_test.cpp).
namespace {

using augusta::math::Vec3;
using augusta::protocol::BytesWire;
using augusta::protocol::CapturedCommandWire;
using augusta::protocol::CapturedDeathWire;
using augusta::protocol::CapturedJoinWire;
using augusta::protocol::CapturedLeaveWire;
using augusta::protocol::CapturedMatchEndWire;
using augusta::protocol::CaptureHeaderWire;
using augusta::protocol::CaptureRecordTypeWire;
using augusta::protocol::CaptureRecordWire;
using augusta::protocol::CommandsWire;
using augusta::protocol::CommandWire;
using augusta::protocol::DecodeCaptureRecord;
using augusta::protocol::DecodeError;
using augusta::protocol::Encode;
using augusta::protocol::EncodeCaptureRecord;
using augusta::protocol::EncodeError;
using augusta::protocol::kCaptureFormatVersion;
using augusta::protocol::kMaxCharacterNameLength;
using augusta::protocol::kMaxEngineVersionLength;
using augusta::protocol::PackHashWire;
using augusta::protocol::SessionIdWire;
using augusta::protocol::StanceWire;
using augusta::protocol::TypeOf;

CaptureRecordWire RoundTrip(const CaptureRecordWire& record) {
  const auto decoded = DecodeCaptureRecord(EncodeCaptureRecord(record).value());
  EXPECT_TRUE(decoded.has_value());
  return decoded.value_or(CaptureRecordWire{});
}

PackHashWire FilledPackHash(std::uint8_t first) {
  PackHashWire hash{};
  for (std::size_t i = 0; i < hash.size(); ++i) {
    hash[i] = static_cast<std::byte>(first + i);
  }
  return hash;
}

CaptureHeaderWire Header() {
  return CaptureHeaderWire{.server_pack = FilledPackHash(1),
                           .client_pack = FilledPackHash(100),
                           .engine_version = "2.0.1",
                           .started_unix_ms = 1'791'000'000'123,
                           .format_version = kCaptureFormatVersion,
                           .tick_rate_hz = 60};
}

// A command with every field away from its default, each on its grid.
CapturedCommandWire Command() {
  return CapturedCommandWire{.command = CommandWire{.direction = Vec3{0.5F, 0.0F, -1.0F},
                                                    .yaw = 1.25F,
                                                    .pitch = -0.5F,
                                                    .seen_fraction = 0.5F,
                                                    .flags = CommandWire::kSprint | CommandWire::kFire,
                                                    .desired_stance = StanceWire::kCrouching,
                                                    .seen_age = 0},
                             .offset = 70'000,
                             .seen_offset = -3,
                             .player = 2};
}

TEST(ProtocolCaptureTest, AHeaderRoundTrips) { EXPECT_EQ(std::get<CaptureHeaderWire>(RoundTrip(Header())), Header()); }

TEST(ProtocolCaptureTest, AJoinRoundTrips) {
  const CapturedJoinWire join{.spawn = Vec3{4.0F, 0.5F, -12.25F},
                              .offset = 0,
                              .session = SessionIdWire{7},
                              .character = "soldier",
                              .player = 1};
  EXPECT_EQ(std::get<CapturedJoinWire>(RoundTrip(join)), join);
}

TEST(ProtocolCaptureTest, ACommandRoundTrips) {
  EXPECT_EQ(std::get<CapturedCommandWire>(RoundTrip(Command())), Command());
}

TEST(ProtocolCaptureTest, ALeaveADeathAndAMatchEndRoundTrip) {
  const CapturedLeaveWire leave{.offset = 12, .player = 3};
  const CapturedDeathWire death{.offset = 40, .victim = 1, .killer = 2};
  const CapturedMatchEndWire end{.offset = 41, .winner = 2};
  EXPECT_EQ(std::get<CapturedLeaveWire>(RoundTrip(leave)), leave);
  EXPECT_EQ(std::get<CapturedDeathWire>(RoundTrip(death)), death);
  EXPECT_EQ(std::get<CapturedMatchEndWire>(RoundTrip(end)), end);
}

TEST(ProtocolCaptureTest, EachRecordStartsWithItsType) {
  EXPECT_EQ(TypeOf(CaptureRecordWire{Header()}), CaptureRecordTypeWire::kHeader);
  EXPECT_EQ(TypeOf(CaptureRecordWire{Command()}), CaptureRecordTypeWire::kCommand);
  const BytesWire bytes = EncodeCaptureRecord(CapturedLeaveWire{.offset = 1, .player = 1}).value();
  EXPECT_EQ(bytes.front(), static_cast<std::byte>(CaptureRecordTypeWire::kLeave));
}

// ADR-0050: a captured Command is the very bytes a Commands message carries it in.
TEST(ProtocolCaptureTest, ACapturedCommandIsEncodedAsACommandsMessageEncodesIt) {
  const BytesWire record = EncodeCaptureRecord(Command()).value();
  const BytesWire message =
      Encode(CommandsWire{.commands = {{.sequence = 0, .command = Command().command}}, .seen_tick = 0}).value();
  // The message's command: after its type, its count and its 8-byte sequence, up to its Seen tick.
  constexpr std::size_t kCommandStart = 1 + 1 + 8;
  constexpr std::size_t kSeenTickSize = 8;
  const BytesWire command(message.begin() + kCommandStart, message.end() - kSeenTickSize);
  EXPECT_NE(std::search(record.begin(), record.end(), command.begin(), command.end()), record.end());
}

// ADR-0050: a record is never a message.
// Requirements: NFR-12
TEST(ProtocolCaptureTest, TheMessageDecoderNeverTakesACaptureRecord) {
  const BytesWire bytes = EncodeCaptureRecord(Header()).value();
  EXPECT_FALSE(augusta::protocol::Decode(bytes).has_value());
}

// Requirements: NFR-12
TEST(ProtocolCaptureTest, AnEmptyPayloadIsNoRecord) {
  EXPECT_EQ(DecodeCaptureRecord(BytesWire{}).error(), DecodeError::kEmpty);
}

// Requirements: NFR-12
TEST(ProtocolCaptureTest, AnUnknownTypeIsNoRecord) {
  EXPECT_EQ(DecodeCaptureRecord(BytesWire{std::byte{0}}).error(), DecodeError::kUnknownType);
  EXPECT_EQ(DecodeCaptureRecord(BytesWire{std::byte{7}}).error(), DecodeError::kUnknownType);
}

// Requirements: NFR-12
TEST(ProtocolCaptureTest, ATruncatedRecordIsRefused) {
  BytesWire bytes = EncodeCaptureRecord(Command()).value();
  bytes.pop_back();
  EXPECT_EQ(DecodeCaptureRecord(bytes).error(), DecodeError::kTruncated);
}

// Requirements: NFR-12
TEST(ProtocolCaptureTest, BytesAfterARecordAreRefused) {
  BytesWire bytes = EncodeCaptureRecord(CapturedMatchEndWire{.offset = 1, .winner = 0}).value();
  bytes.push_back(std::byte{0});
  EXPECT_EQ(DecodeCaptureRecord(bytes).error(), DecodeError::kTrailingBytes);
}

// Requirements: NFR-12
TEST(ProtocolCaptureTest, ACommandWithAFlagNoCommandHasIsRefused) {
  BytesWire bytes = EncodeCaptureRecord(Command()).value();
  // The record ends with its command, whose flags and stance byte is followed
  // by its Seen age and Seen fraction.
  const auto fraction_bytes = static_cast<std::size_t>(augusta::math::kFractionGrid.bytes);
  bytes[bytes.size() - 2 - fraction_bytes] |= std::byte{0x80};
  EXPECT_EQ(DecodeCaptureRecord(bytes).error(), DecodeError::kInvalidEnum);
}

TEST(ProtocolCaptureEncodeTest, AnEngineVersionLongerThanAllowedIsNotEncoded) {
  CaptureHeaderWire header = Header();
  header.engine_version = std::string(kMaxEngineVersionLength + 1, 'v');
  EXPECT_EQ(EncodeCaptureRecord(header).error(), EncodeError::kFieldTooLong);
}

TEST(ProtocolCaptureEncodeTest, ACharacterLongerThanAllowedIsNotEncoded) {
  const CapturedJoinWire join{.character = std::string(kMaxCharacterNameLength + 1, 'c')};
  EXPECT_EQ(EncodeCaptureRecord(join).error(), EncodeError::kFieldTooLong);
}

TEST(ProtocolCaptureEncodeTest, ACommandFlagNoCommandHasIsNotEncoded) {
  CapturedCommandWire command = Command();
  command.command.flags = 0x40;
  EXPECT_EQ(EncodeCaptureRecord(command).error(), EncodeError::kReservedBits);
}

}  // namespace
