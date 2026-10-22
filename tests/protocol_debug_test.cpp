#include <cstddef>
#include <cstdint>
#include <variant>

#include <gtest/gtest.h>

#include "augusta/protocol.h"
#include "protobuf_bytes.h"

// The messages of a Development build (ADR-0038, ADR-0074), which a Shipping
// build's codec does not have: built only into a Development build's protocol
// tests.
namespace {

using augusta::protobuf_bytes::LengthField;
using augusta::protobuf_bytes::VarintField;
using augusta::protocol::BytesWire;
using augusta::protocol::DebugSubscribeWire;
using augusta::protocol::Decode;
using augusta::protocol::DecodeError;
using augusta::protocol::Encode;
using augusta::protocol::MessageTypeWire;
using augusta::protocol::MessageWire;

// The payload of a message of type whose body holds fields.
BytesWire Envelope(MessageTypeWire type, const BytesWire& fields) {
  return LengthField(static_cast<int>(type), fields);
}

TEST(ProtocolDebugTest, ADebugSubscribeIsTheEnvelopesEighteenthMember) {
  EXPECT_EQ(static_cast<int>(MessageTypeWire::kDebugSubscribe), 18);
  EXPECT_EQ(augusta::protocol::TypeOf(MessageWire{DebugSubscribeWire{}}), MessageTypeWire::kDebugSubscribe);
}

// Every bit of the byte travels, a bit no stream uses yet included: the server
// ignores those, so a later stream's bit is never a malformed message.
TEST(ProtocolDebugTest, ADebugSubscribeRoundTripsAnyByteOfFlags) {
  for (const std::uint8_t flags :
       {std::uint8_t{0}, DebugSubscribeWire::kShotJudgements, std::uint8_t{0x80}, std::uint8_t{0xFF}}) {
    const auto decoded = Decode(Encode(DebugSubscribeWire{.flags = flags}).value());

    ASSERT_TRUE(decoded.has_value()) << +flags;
    ASSERT_TRUE(std::holds_alternative<DebugSubscribeWire>(*decoded)) << +flags;
    EXPECT_EQ(std::get<DebugSubscribeWire>(*decoded).flags, flags);
  }
}

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

}  // namespace
