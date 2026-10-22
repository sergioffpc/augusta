#include <cstdint>
#include <variant>

#include <gtest/gtest.h>

#include "augusta/link_messages.h"
#include "augusta/link_types.h"
#include "augusta/protocol.h"

// The Link's conversions of a Development build's messages (ADR-0038,
// ADR-0074), as link_messages_test.cpp has the rest's.
namespace {

using augusta::link::DebugSubscribe;
using augusta::link::ToWire;

// message as the other peer decodes it.
augusta::protocol::MessageWire ThroughTheWire(const augusta::protocol::MessageWire& message) {
  return augusta::protocol::Decode(augusta::protocol::Encode(message).value()).value();
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

}  // namespace
