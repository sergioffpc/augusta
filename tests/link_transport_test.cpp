#include <cstddef>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/failure.h"
#include "augusta/link.h"
#include "augusta/link_messages.h"
#include "augusta/link_types.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/protocol.h"
#include "connection_stats.h"
#include "peer_message.h"
#include "reliability.h"

// What the Link decides about the transport beneath it, apart from a live
// connection: which reliability each message travels with (ADR-0038's table),
// the connection's numbers in the Link's own types, what a payload a peer sent
// is to its server, and what a server's message is once encoded.
namespace {

using augusta::link::ConnectionStats;
using augusta::link::FromTransport;
using augusta::link::MessageType;
using augusta::link::ReliabilityOf;
using augusta::networking::Reliability;

// A newer one supersedes each of these, so none is retransmitted: a tick's
// Commands, an Authoritative State and a Replay view (ADR-0051).
TEST(ReliabilityOfTest, PerTickMessagesTravelUnreliably) {
  for (const MessageType type : {MessageType::kCommands, MessageType::kAuthoritativeState, MessageType::kReplayView}) {
    EXPECT_EQ(ReliabilityOf(type), Reliability::kUnreliable) << static_cast<int>(type);
  }
}

// Each of these must arrive, or the two sides disagree.
TEST(ReliabilityOfTest, EveryOtherMessageTravelsReliably) {
  for (const MessageType type : {
           MessageType::kJoinRequest,
           MessageType::kJoinAccepted,
           MessageType::kJoinRefused,
           MessageType::kLobby,
           MessageType::kReady,
           MessageType::kMatchStart,
           MessageType::kMatchEnd,
           MessageType::kShot,
           MessageType::kHitConfirmation,
           MessageType::kDeath,
           MessageType::kReplayListRequest,
           MessageType::kReplayList,
           MessageType::kReplayRequest,
           MessageType::kReenactRequest,
       }) {
    EXPECT_EQ(ReliabilityOf(type), Reliability::kReliable) << static_cast<int>(type);
  }
}

// Every number reaches the Link's own type as the transport measured it, the
// "not measured yet" sentinels included.
TEST(FromTransportTest, KeepsEveryNumberTheTransportMeasured) {
  const ConnectionStats stats = FromTransport(augusta::networking::ConnectionStats{
      .ping_ms = 42,
      .quality_local = 0.75F,
      .quality_remote = -1.0F,
      .in_bytes_per_sec = 1200.0F,
      .out_bytes_per_sec = 3400.0F,
      .max_jitter_us = 5600,
      .pending_bytes = 78,
  });
  EXPECT_EQ(stats.ping_ms, 42);
  EXPECT_FLOAT_EQ(stats.quality_local, 0.75F);
  EXPECT_FLOAT_EQ(stats.quality_remote, -1.0F);
  EXPECT_FLOAT_EQ(stats.in_bytes_per_sec, 1200.0F);
  EXPECT_FLOAT_EQ(stats.out_bytes_per_sec, 3400.0F);
  EXPECT_EQ(stats.max_jitter_us, 5600);
  EXPECT_EQ(stats.pending_bytes, 78);
}

constexpr augusta::link::PeerId kPeer{7};

// A peer's payload that is no message at all is only its bytes, and the
// decoder's words for why, which a server logs as it judges the peer.
TEST(ReadFromTest, APayloadThatDecodesToNoMessageSaysWhy) {
  const std::vector<std::byte> garbage{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
  const augusta::link::PeerMessage message = augusta::link::ReadFrom(kPeer, garbage);
  EXPECT_EQ(message.from, kPeer);
  EXPECT_EQ(message.bytes, 3U);
  ASSERT_FALSE(message.type.has_value());
  EXPECT_FALSE(message.type.error().empty());
  EXPECT_FALSE(message.message.has_value());
}

// A message only a server sends still has its type, which a server counts,
// but says nothing a server takes in.
TEST(ReadFromTest, AServersMessageHasItsTypeAndSaysNothing) {
  const auto payload = augusta::protocol::Encode(augusta::link::ToWire(augusta::link::Shot{})).value();
  const augusta::link::PeerMessage message = augusta::link::ReadFrom(kPeer, payload);
  EXPECT_EQ(message.bytes, payload.size());
  EXPECT_EQ(message.type, MessageType::kShot);
  EXPECT_FALSE(message.message.has_value());
}

// A Reenact request is a join that names its spawn, counted as its own type.
TEST(ReadFromTest, AClientsMessageIsWhatItSaysUnderItsOwnType) {
  const augusta::link::JoinRequest join{.engine_version = "1.2.3",
                                        .client_pack = {},
                                        .character = "soldier",
                                        .spawn = augusta::math::Vec3(1.0F, 2.0F, 3.0F)};
  const auto payload = augusta::protocol::Encode(augusta::link::ToWire(join)).value();
  const augusta::link::PeerMessage message = augusta::link::ReadFrom(kPeer, payload);
  EXPECT_EQ(message.type, MessageType::kReenactRequest);
  ASSERT_TRUE(message.message.has_value());
  EXPECT_EQ(std::get<augusta::link::JoinRequest>(*message.message).character, "soldier");
}

// A server's message is encoded once, as the protocol has it, under its type,
// so it is counted and sent to as many peers as need it.
TEST(EncodeTest, AServersMessageIsItsProtocolBytesUnderItsType) {
  const augusta::link::Shot shot{.shooter = augusta::link::EntityId{4}, .tick = 90};
  const auto encoded = augusta::link::Encode(shot);
  ASSERT_TRUE(encoded.has_value());
  EXPECT_EQ(encoded->Type(), MessageType::kShot);
  const auto expected = augusta::protocol::Encode(augusta::link::ToWire(shot)).value();
  EXPECT_EQ(std::vector<std::byte>(encoded->Payload().begin(), encoded->Payload().end()), expected);
}

// A refusal no case names is no message the protocol carries: the broken
// invariant, and nothing to send.
TEST(EncodeTest, AMessageTheProtocolCannotCarryIsABrokenInvariant) {
  constexpr auto kCorrupted = static_cast<augusta::link::JoinRefusal>(200);
  const auto encoded = augusta::link::Encode(kCorrupted);
  ASSERT_FALSE(encoded.has_value());
  EXPECT_EQ(encoded.error().code, augusta::failure::Code::kInvariantViolated);
}

}  // namespace
