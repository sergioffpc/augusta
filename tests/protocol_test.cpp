#include "augusta/protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <variant>

#include <gtest/gtest.h>

// The codec is pure: every case here is bytes in, message or error out.
namespace {

using augusta::protocol::Bytes;
using augusta::protocol::Decode;
using augusta::protocol::DecodeError;
using augusta::protocol::Encode;
using augusta::protocol::JoinAccepted;
using augusta::protocol::JoinRefusal;
using augusta::protocol::JoinRefused;
using augusta::protocol::JoinRequest;
using augusta::protocol::kMaxEngineVersionLength;
using augusta::protocol::Message;
using augusta::protocol::MessageType;
using augusta::protocol::SessionId;

Bytes BytesOf(std::initializer_list<std::uint8_t> values) {
  Bytes bytes;
  for (const std::uint8_t value : values) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return bytes;
}

constexpr auto kJoinRequestType = static_cast<std::uint8_t>(MessageType::kJoinRequest);
constexpr auto kJoinAcceptedType = static_cast<std::uint8_t>(MessageType::kJoinAccepted);
constexpr auto kJoinRefusedType = static_cast<std::uint8_t>(MessageType::kJoinRefused);

Message RoundTrip(const Message& message) {
  const auto decoded = Decode(Encode(message));
  EXPECT_TRUE(decoded.has_value());
  return decoded.value_or(Message{});
}

TEST(ProtocolTest, JoinRequestRoundTrips) {
  const auto decoded = RoundTrip(JoinRequest{.engine_version = "0.1.0"});

  ASSERT_TRUE(std::holds_alternative<JoinRequest>(decoded));
  EXPECT_EQ(std::get<JoinRequest>(decoded).engine_version, "0.1.0");
}

TEST(ProtocolTest, JoinRequestWithTheLongestVersionRoundTrips) {
  const std::string longest(kMaxEngineVersionLength, 'v');

  const auto decoded = RoundTrip(JoinRequest{.engine_version = longest});

  EXPECT_EQ(std::get<JoinRequest>(decoded).engine_version, longest);
}

TEST(ProtocolTest, JoinRequestWithAnEmptyVersionRoundTrips) {
  const auto decoded = RoundTrip(JoinRequest{});

  EXPECT_EQ(std::get<JoinRequest>(decoded).engine_version, "");
}

TEST(ProtocolTest, JoinAcceptedRoundTrips) {
  const auto decoded = RoundTrip(JoinAccepted{.session = static_cast<SessionId>(0xA1B2C3D4U)});

  ASSERT_TRUE(std::holds_alternative<JoinAccepted>(decoded));
  EXPECT_EQ(std::get<JoinAccepted>(decoded).session, static_cast<SessionId>(0xA1B2C3D4U));
}

TEST(ProtocolTest, JoinRefusedRoundTripsEveryReason) {
  for (const JoinRefusal reason : {JoinRefusal::kVersionMismatch, JoinRefusal::kMatchFull}) {
    const auto decoded = RoundTrip(JoinRefused{.reason = reason});

    ASSERT_TRUE(std::holds_alternative<JoinRefused>(decoded));
    EXPECT_EQ(std::get<JoinRefused>(decoded).reason, reason);
  }
}

TEST(ProtocolTest, FieldsAreFixedWidthLittleEndian) {
  EXPECT_EQ(Encode(JoinAccepted{.session = static_cast<SessionId>(0x04030201U)}),
            BytesOf({kJoinAcceptedType, 0x01, 0x02, 0x03, 0x04}));
  EXPECT_EQ(Encode(JoinRefused{.reason = JoinRefusal::kMatchFull}), BytesOf({kJoinRefusedType, 2}));
  EXPECT_EQ(Encode(JoinRequest{.engine_version = "ab"}), BytesOf({kJoinRequestType, 2, 'a', 'b'}));
}

TEST(ProtocolTest, AnEmptyPayloadIsEmpty) { EXPECT_EQ(Decode(Bytes{}).error(), DecodeError::kEmpty); }

TEST(ProtocolTest, AnUnknownTypeIsRejected) {
  EXPECT_EQ(Decode(BytesOf({0})).error(), DecodeError::kUnknownType);
  EXPECT_EQ(Decode(BytesOf({4, 0, 0, 0, 0})).error(), DecodeError::kUnknownType);
  EXPECT_EQ(Decode(BytesOf({0xFF})).error(), DecodeError::kUnknownType);
}

TEST(ProtocolTest, EveryTruncationOfEveryMessageIsTruncatedNotACrash) {
  const std::array<Message, 3> messages = {JoinRequest{.engine_version = "0.1.0"},
                                           JoinAccepted{.session = static_cast<SessionId>(7)},
                                           JoinRefused{.reason = JoinRefusal::kMatchFull}};
  for (const Message& message : messages) {
    const Bytes whole = Encode(message);
    for (std::size_t length = 1; length < whole.size(); ++length) {
      const Bytes cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(length));
      EXPECT_EQ(Decode(cut).error(), DecodeError::kTruncated)
          << "type " << static_cast<int>(whole[0]) << " cut to " << length;
    }
  }
}

TEST(ProtocolTest, ALengthPointingPastThePayloadIsTruncatedWithoutReadingIt) {
  // Claims 32 bytes of version, supplies 2.
  EXPECT_EQ(Decode(BytesOf({kJoinRequestType, 32, 'a', 'b'})).error(), DecodeError::kTruncated);
}

TEST(ProtocolTest, AVersionLongerThanAllowedIsTooLongEvenWhenAllOfItIsPresent) {
  Bytes payload = BytesOf({kJoinRequestType, static_cast<std::uint8_t>(kMaxEngineVersionLength + 1)});
  payload.resize(payload.size() + kMaxEngineVersionLength + 1, static_cast<std::byte>('v'));

  EXPECT_EQ(Decode(payload).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, ALengthOf255IsRejectedBeforeAnythingIsAllocatedForIt) {
  EXPECT_EQ(Decode(BytesOf({kJoinRequestType, 255})).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, ARefusalReasonOutsideTheEnumerationIsInvalid) {
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 0})).error(), DecodeError::kInvalidEnum);
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 3})).error(), DecodeError::kInvalidEnum);
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 0xFF})).error(), DecodeError::kInvalidEnum);
}

TEST(ProtocolTest, BytesAfterAMessageAreTrailing) {
  EXPECT_EQ(Decode(BytesOf({kJoinRequestType, 0, 0})).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(BytesOf({kJoinAcceptedType, 1, 0, 0, 0, 0})).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 1, 1})).error(), DecodeError::kTrailingBytes);
}

TEST(ProtocolTest, EveryErrorAndRefusalHasADescription) {
  for (const DecodeError error : {DecodeError::kEmpty, DecodeError::kUnknownType, DecodeError::kTruncated,
                                  DecodeError::kTrailingBytes, DecodeError::kInvalidEnum, DecodeError::kFieldTooLong}) {
    EXPECT_FALSE(augusta::protocol::DescribeDecodeError(error).empty());
  }
  for (const JoinRefusal reason : {JoinRefusal::kVersionMismatch, JoinRefusal::kMatchFull}) {
    EXPECT_FALSE(augusta::protocol::DescribeJoinRefusal(reason).empty());
  }
}

}  // namespace
