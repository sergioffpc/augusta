#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/protocol.h"
#include "protobuf_bytes.h"

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
using augusta::protocol::Decode;
using augusta::protocol::DecodeCaptureRecord;
using augusta::protocol::DecodeError;
using augusta::protocol::Encode;
using augusta::protocol::EncodeCaptureRecord;
using augusta::protocol::EncodeError;
using augusta::protocol::kMaxCharacterNameLength;
using augusta::protocol::kMaxEngineVersionLength;
using augusta::protocol::PackHashWire;
using augusta::protocol::SessionIdWire;
using augusta::protocol::StanceWire;
using augusta::protocol::TypeOf;

using augusta::protobuf_bytes::Cat;
using augusta::protobuf_bytes::LengthField;
using augusta::protobuf_bytes::SignedField;
using augusta::protobuf_bytes::VarintField;

// Each record is its augusta.CaptureRecord member, numbered 1000 plus its type.
constexpr int kFirstRecordNumber = 1000;

int NumberOf(CaptureRecordTypeWire type) { return kFirstRecordNumber + static_cast<int>(type); }

// The payload of a record of type whose member holds fields.
BytesWire Record(CaptureRecordTypeWire type, const BytesWire& fields) { return LengthField(NumberOf(type), fields); }

// The payload of a captured Command whose command holds command's fields.
BytesWire CapturedCommandRecord(const BytesWire& command) {
  return Record(CaptureRecordTypeWire::kCommand,
                Cat({VarintField(1, 70'000), VarintField(2, 2), SignedField(3, -3), LengthField(4, command)}));
}

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

// Requirements: US-21, NFR-20
TEST(ProtocolCaptureTest, AHeaderRoundTrips) { EXPECT_EQ(std::get<CaptureHeaderWire>(RoundTrip(Header())), Header()); }

// Requirements: US-21, NFR-20
TEST(ProtocolCaptureTest, AJoinRoundTrips) {
  const CapturedJoinWire join{.spawn = Vec3{4.0F, 0.5F, -12.25F},
                              .offset = 0,
                              .session = SessionIdWire({0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD4, 0x70, 0x00, 0x80, 0x00,
                                                        0x00, 0x00, 0x00, 0x00, 0x00, 0x07}),
                              .character = "soldier",
                              .player = 1};
  EXPECT_EQ(std::get<CapturedJoinWire>(RoundTrip(join)), join);
}

// Requirements: US-21, NFR-20
TEST(ProtocolCaptureTest, ACommandRoundTrips) {
  EXPECT_EQ(std::get<CapturedCommandWire>(RoundTrip(Command())), Command());
}

// Requirements: US-21, NFR-20
TEST(ProtocolCaptureTest, ALeaveADeathAndAMatchEndRoundTrip) {
  const CapturedLeaveWire leave{.offset = 12, .player = 3};
  const CapturedDeathWire death{.offset = 40, .victim = 1, .killer = 2};
  const CapturedMatchEndWire end{.offset = 41, .winner = 2};
  EXPECT_EQ(std::get<CapturedLeaveWire>(RoundTrip(leave)), leave);
  EXPECT_EQ(std::get<CapturedDeathWire>(RoundTrip(death)), death);
  EXPECT_EQ(std::get<CapturedMatchEndWire>(RoundTrip(end)), end);
}

// Requirements: NFR-20
TEST(ProtocolCaptureTest, EachRecordIsItsTypesMember) {
  EXPECT_EQ(TypeOf(CaptureRecordWire{Header()}), CaptureRecordTypeWire::kHeader);
  EXPECT_EQ(TypeOf(CaptureRecordWire{Command()}), CaptureRecordTypeWire::kCommand);
  const BytesWire leave = Record(CaptureRecordTypeWire::kLeave, Cat({VarintField(1, 12), VarintField(2, 3)}));
  EXPECT_EQ(DecodeCaptureRecord(leave).value(), CaptureRecordWire(CapturedLeaveWire{.offset = 12, .player = 3}));
}

// ADR-0050: a captured Command is the very augusta.Command a Commands message
// carries, so the same command's fields read as the same command in either.
// Requirements: US-21, NFR-20
TEST(ProtocolCaptureTest, ACapturedCommandCarriesTheCommandACommandsMessageDoes) {
  // Every field away from its default: a sequence, which a capture ignores,
  // direction (2, 0, -4) and yaw 5 and pitch -2 steps, sprint and fire with
  // a crouch, and a Seen fraction of 64 steps.
  const BytesWire command = Cat({VarintField(1, 9), SignedField(2, 2), SignedField(4, -4), SignedField(5, 5),
                                 SignedField(6, -2), VarintField(7, 0x15), VarintField(9, 64)});
  const BytesWire message =
      LengthField(static_cast<int>(augusta::protocol::MessageTypeWire::kCommands), LengthField(1, command));

  const CommandWire sent = std::get<CommandsWire>(Decode(message).value()).commands.at(0).command;
  const CommandWire captured =
      std::get<CapturedCommandWire>(DecodeCaptureRecord(CapturedCommandRecord(command)).value()).command;
  EXPECT_EQ(captured, sent);
  EXPECT_NE(captured, CommandWire{});
}

// ADR-0050: a record is never a message.
// Requirements: NFR-12
TEST(ProtocolCaptureTest, TheMessageDecoderNeverTakesACaptureRecord) {
  const BytesWire bytes = EncodeCaptureRecord(Header()).value();
  EXPECT_FALSE(Decode(bytes).has_value());
}

// Requirements: NFR-12
TEST(ProtocolCaptureTest, AnEmptyPayloadIsNoRecord) {
  EXPECT_EQ(DecodeCaptureRecord(BytesWire{}).error(), DecodeError::kUnknownType);
}

// The envelope's members number from 1001 to 1006: a member past them is a
// field this build does not know, ignored, which leaves the envelope with no
// record.
// Requirements: NFR-12
TEST(ProtocolCaptureTest, AnEnvelopeWithNoMemberThisBuildKnowsIsNoRecord) {
  EXPECT_TRUE(DecodeCaptureRecord(Record(CaptureRecordTypeWire::kMatchEnd, {})).has_value());
  EXPECT_EQ(DecodeCaptureRecord(LengthField(1007, {})).error(), DecodeError::kUnknownType);
  EXPECT_EQ(DecodeCaptureRecord(VarintField(1, 1)).error(), DecodeError::kUnknownType);
}

// A message's envelope numbers its members from 1 (ADR-0007), so a message is
// never a record.
// Requirements: NFR-12
TEST(ProtocolCaptureTest, AMessageIsNoRecord) {
  const BytesWire message =
      Encode(CommandsWire{.commands = {{.sequence = 1, .command = Command().command}}, .seen_tick = 4}).value();
  EXPECT_EQ(DecodeCaptureRecord(message).error(), DecodeError::kUnknownType);
}

// Requirements: NFR-12
TEST(ProtocolCaptureTest, ATruncatedRecordIsRefused) {
  BytesWire bytes = EncodeCaptureRecord(Command()).value();
  bytes.pop_back();
  EXPECT_EQ(DecodeCaptureRecord(bytes).error(), DecodeError::kMalformed);
}

// Requirements: NFR-12
TEST(ProtocolCaptureTest, BytesAfterARecordAreRefused) {
  BytesWire bytes = EncodeCaptureRecord(CapturedMatchEndWire{.offset = 1, .winner = 0}).value();
  // A zero byte, which protobuf reads as the tag of a field numbered 0, which
  // no record has.
  bytes.push_back(std::byte{0});
  EXPECT_EQ(DecodeCaptureRecord(bytes).error(), DecodeError::kMalformed);
}

// Requirements: NFR-12
TEST(ProtocolCaptureTest, ACommandWithAFlagNoCommandHasIsRefused) {
  // Bit 6 is past the command's flags (bits 0-3) and its stance (bits 4-5).
  EXPECT_EQ(DecodeCaptureRecord(CapturedCommandRecord(VarintField(7, 0x40))).error(), DecodeError::kReservedBits);
}

// Requirements: NFR-20
TEST(ProtocolCaptureEncodeTest, AnEngineVersionLongerThanAllowedIsNotEncoded) {
  CaptureHeaderWire header = Header();
  header.engine_version = std::string(kMaxEngineVersionLength + 1, 'v');
  EXPECT_EQ(EncodeCaptureRecord(header).error(), EncodeError::kFieldTooLong);
}

// Requirements: NFR-20
TEST(ProtocolCaptureEncodeTest, ACharacterLongerThanAllowedIsNotEncoded) {
  const CapturedJoinWire join{.character = std::string(kMaxCharacterNameLength + 1, 'c')};
  EXPECT_EQ(EncodeCaptureRecord(join).error(), EncodeError::kFieldTooLong);
}

// Requirements: NFR-20
TEST(ProtocolCaptureEncodeTest, ACommandFlagNoCommandHasIsNotEncoded) {
  CapturedCommandWire command = Command();
  command.command.flags = 0x40;
  EXPECT_EQ(EncodeCaptureRecord(command).error(), EncodeError::kReservedBits);
}

}  // namespace
