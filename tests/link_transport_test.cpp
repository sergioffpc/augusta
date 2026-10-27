#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/failure.h"
#include "augusta/link.h"
#include "augusta/link_messages.h"
#include "augusta/link_types.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/protocol.h"
#include "augusta/tick.h"
#include "connection_stats.h"
#include "peer_message.h"
#include "raw_link.h"
#include "reliability.h"

// What the Link decides about the transport beneath it, apart from a live
// connection: which reliability each message travels with (ADR-0038's table),
// the connection's numbers in the Link's own types, what a payload a peer sent
// is to its server, and what a server's message is once encoded. Then, over a
// real loopback connection to a raw server, what a Client counts it carried.
namespace {

class TransportEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { ASSERT_TRUE(augusta::link::Init().has_value()); }
  void TearDown() override { augusta::link::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kTransportEnvironment =
    ::testing::AddGlobalTestEnvironment(new TransportEnvironment);

using augusta::link::ConnectionStats;
using augusta::link::FromTransport;
using augusta::link::MessageType;
using augusta::link::ReliabilityOf;
using augusta::networking::Reliability;

// A newer one supersedes each of these, so none is retransmitted: a tick's
// Commands and an Authoritative State.
TEST(ReliabilityOfTest, PerTickMessagesTravelUnreliably) {
  for (const MessageType type : {MessageType::kCommands, MessageType::kAuthoritativeState}) {
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

// A tick's Authoritative State, its bodies encoded once, readdressed to another
// recipient is what encoding it for that recipient makes, byte for byte: every
// recipient is sent the bytes it always was.
TEST(ReaddressedTest, IsTheStateEncodedForItsRecipient) {
  const augusta::link::AuthoritativeState first{
      .tick = 120,
      .acknowledged_sequence = 17,
      .bodies = {{.entity = augusta::link::EntityId{3}, .body = {}, .yaw = 0.5F},
                 {.entity = augusta::link::EntityId{4}, .body = {}, .yaw = -1.25F}},
      .rifle = {},
      .health = 100.0F,
      .queued_commands = 2};
  augusta::link::AuthoritativeState second = first;
  second.acknowledged_sequence = 99;
  second.rifle.rounds = 12;
  second.health = 40.0F;
  second.queued_commands = 0;

  const auto shared = augusta::link::Encode(first);
  ASSERT_TRUE(shared.has_value());
  const auto readdressed = augusta::link::Readdressed(*shared, second);
  ASSERT_TRUE(readdressed.has_value());

  EXPECT_EQ(readdressed->Type(), MessageType::kAuthoritativeState);
  const auto expected = augusta::protocol::Encode(augusta::link::ToWire(second)).value();
  EXPECT_EQ(std::vector<std::byte>(readdressed->Payload().begin(), readdressed->Payload().end()), expected);
}

// Only an Authoritative State has a recipient's own fields to readdress.
TEST(ReaddressedTest, AnyOtherMessageIsABrokenInvariant) {
  const auto shot = augusta::link::Encode(augusta::link::Shot{});
  ASSERT_TRUE(shot.has_value());
  const auto readdressed = augusta::link::Readdressed(*shot, augusta::link::AuthoritativeState{});
  ASSERT_FALSE(readdressed.has_value());
  EXPECT_EQ(readdressed.error().code, augusta::failure::Code::kInvariantViolated);
  // Named by its message type's name, as a log reader knows it.
  ASSERT_EQ(readdressed.error().context.size(), 1U);
  EXPECT_EQ(readdressed.error().context[0].key, "message_type");
  EXPECT_EQ(readdressed.error().context[0].value, "shot");
}

constexpr auto kPollInterval = std::chrono::milliseconds(5);
constexpr auto kDeadline = std::chrono::seconds(5);

// Calls poll until done() holds or the deadline passes; returns whether it held.
template <typename Poll, typename Done>
bool PollUntil(Poll poll, Done done) {
  const auto deadline = std::chrono::steady_clock::now() + kDeadline;
  while (std::chrono::steady_clock::now() < deadline) {
    poll();
    if (done()) {
      return true;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  return false;
}

// The bytes a server's message is on the wire.
augusta::link::RawPayload BytesOf(const augusta::protocol::MessageWire& message) {
  return augusta::protocol::Encode(message).value();
}

// A Client connected over loopback to a raw server, which sends it any bytes
// and reads what it sent as they are.
class ClientTrafficTest : public ::testing::Test {
 protected:
  ClientTrafficTest() : server_(augusta::link::Endpoint{.address = "127.0.0.1:0"}) {}

  void SetUp() override { ASSERT_TRUE(Connect()); }

  // Connects the client and serves its handshake; returns whether both ends
  // are connected within the deadline.
  bool Connect() {
    peer_.reset();
    client_.Connect(server_.LocalEndpoint());
    return PollUntil(
        [this] {
          client_.PumpEvents();
          for (const augusta::link::PeerEvent& event : server_.PumpEvents()) {
            if (event.type == augusta::link::PeerEventType::kConnectRequested) {
              server_.Accept(event.peer);
            } else if (event.type == augusta::link::PeerEventType::kConnected) {
              peer_ = event.peer;
            }
          }
        },
        [this] { return peer_.has_value() && client_.GetState() == augusta::link::ConnectionState::kConnected; });
  }

  // Sends payloads to the client, then receives until it has taken in
  // expected messages; returns whether it did within the deadline.
  bool ServeUntilReceived(const std::vector<augusta::link::RawPayload>& payloads, std::size_t expected) {
    for (const augusta::link::RawPayload& payload : payloads) {
      if (!server_.Send(*peer_, payload).has_value()) {
        return false;
      }
    }
    std::size_t received = 0;
    return PollUntil(
        [&] {
          client_.PumpEvents();
          received += client_.Receive().value().size();
        },
        [&] { return received >= expected; });
  }

  // What the raw server has received from the client, once count payloads
  // arrived within the deadline; empty if they did not.
  std::vector<augusta::link::RawPayload> ReceivedByServer(std::size_t count) {
    std::vector<augusta::link::RawPayload> payloads;
    PollUntil(
        [&] {
          static_cast<void>(server_.PumpEvents());
          for (augusta::link::RawPeerMessage& message : server_.Receive().value()) {
            payloads.push_back(std::move(message.payload));
          }
        },
        [&] { return payloads.size() >= count; });
    return payloads.size() >= count ? payloads : std::vector<augusta::link::RawPayload>{};
  }

  augusta::link::RawServer server_;
  std::optional<augusta::link::PeerId> peer_;
  augusta::link::Client client_;
};

// Every type but the one named has carried nothing, either way.
void ExpectOnlyTypeCarried(const augusta::link::Traffic& traffic, MessageType carried) {
  for (std::size_t index = 0; index < augusta::link::kMessageTypeCount; ++index) {
    if (index != static_cast<std::size_t>(carried)) {
      EXPECT_EQ(traffic.by_type.at(index), augusta::link::TypeTraffic{}) << index;
    }
  }
}

// A message the client sent counts once, under its type, as sent, with the
// bytes the server received.
// Requirements: US-29
TEST_F(ClientTrafficTest, ASentMessageCountsOnceUnderItsTypeAsSent) {
  ASSERT_EQ(client_.Send(augusta::link::Ready{.version = 3}), augusta::link::SendOutcome::kAccepted);
  const std::vector<augusta::link::RawPayload> arrived = ReceivedByServer(1);
  ASSERT_EQ(arrived.size(), 1U);

  const augusta::link::Traffic traffic = client_.GetTraffic();
  EXPECT_EQ(traffic.Of(MessageType::kReady).sent,
            (augusta::link::MessageCount{.messages = 1, .bytes = arrived[0].size()}));
  EXPECT_EQ(traffic.Of(MessageType::kReady).received, augusta::link::MessageCount{});
  ExpectOnlyTypeCarried(traffic, MessageType::kReady);
}

// A message the client received counts once, under its type, as received,
// with the bytes the server sent.
// Requirements: US-29
TEST_F(ClientTrafficTest, AReceivedMessageCountsOnceUnderItsTypeAsReceived) {
  const augusta::link::RawPayload shot =
      BytesOf(augusta::link::ToWire(augusta::link::Shot{.shooter = augusta::link::EntityId{4}, .tick = 90}));
  ASSERT_TRUE(ServeUntilReceived({shot}, 1));

  const augusta::link::Traffic traffic = client_.GetTraffic();
  EXPECT_EQ(traffic.Of(MessageType::kShot).received,
            (augusta::link::MessageCount{.messages = 1, .bytes = shot.size()}));
  EXPECT_EQ(traffic.Of(MessageType::kShot).sent, augusta::link::MessageCount{});
  ExpectOnlyTypeCarried(traffic, MessageType::kShot);
}

// Each message counts on its own: two of a type are two, their bytes summed.
// Requirements: US-29
TEST_F(ClientTrafficTest, MessagesOfOneTypeAddUp) {
  const augusta::link::RawPayload first = BytesOf(augusta::link::ToWire(augusta::link::Shot{.tick = 1}));
  const augusta::link::RawPayload second =
      BytesOf(augusta::link::ToWire(augusta::link::Shot{.shooter = augusta::link::EntityId{70000}, .tick = 90000}));
  ASSERT_TRUE(ServeUntilReceived({first, second}, 2));

  EXPECT_EQ(client_.GetTraffic().Of(MessageType::kShot).received,
            (augusta::link::MessageCount{.messages = 2, .bytes = first.size() + second.size()}));
}

// A payload that is no message, and a message only a client sends, are dropped
// by the client and count nowhere; the Lobby sent after them, which it takes
// in, counts.
// Requirements: US-29
TEST_F(ClientTrafficTest, APayloadTheClientDropsCountsNowhere) {
  const augusta::link::RawPayload garbage{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
  const augusta::link::RawPayload ready = BytesOf(augusta::link::ToWire(augusta::link::Ready{.version = 1}));
  const augusta::link::RawPayload lobby =
      BytesOf(augusta::link::ToWire(augusta::link::Lobby{.version = 2, .roster = {}}));
  // Every raw payload travels reliably, in order, so the Lobby arrives last.
  ASSERT_TRUE(ServeUntilReceived({garbage, ready, lobby}, 1));

  const augusta::link::Traffic traffic = client_.GetTraffic();
  EXPECT_EQ(traffic.Of(MessageType::kLobby).received,
            (augusta::link::MessageCount{.messages = 1, .bytes = lobby.size()}));
  ExpectOnlyTypeCarried(traffic, MessageType::kLobby);
}

// A new connection counts from zero, whatever the last one carried.
// Requirements: US-29
TEST_F(ClientTrafficTest, ANewConnectionStartsFromZero) {
  ASSERT_EQ(client_.Send(augusta::link::Ready{.version = 3}), augusta::link::SendOutcome::kAccepted);
  ASSERT_TRUE(ServeUntilReceived({BytesOf(augusta::link::ToWire(augusta::link::Shot{}))}, 1));
  ASSERT_NE(client_.GetTraffic(), augusta::link::Traffic{});

  client_.Disconnect();
  ASSERT_TRUE(Connect());
  EXPECT_EQ(client_.GetTraffic(), augusta::link::Traffic{});
}

// The bytes of an Authoritative State update from tick, carrying bodies
// bodies so that two updates can differ in size.
augusta::link::RawPayload StateBytes(augusta::tick::Tick tick, std::uint32_t bodies = 0) {
  augusta::link::AuthoritativeState state;
  state.tick = tick;
  for (std::uint32_t index = 1; index <= bodies; ++index) {
    augusta::link::EntityBody body;
    body.entity = augusta::link::EntityId{index};
    state.bodies.push_back(body);
  }
  return BytesOf(augusta::link::ToWire(state));
}

// Each update adds its bytes to the total; the last is the newest to arrive,
// the largest the biggest so far, whichever came later.
// Requirements: US-29
TEST_F(ClientTrafficTest, AnAuthoritativeStateUpdateCountsItsSize) {
  const augusta::link::RawPayload large = StateBytes(1, 3);
  const augusta::link::RawPayload small = StateBytes(2);
  ASSERT_LT(small.size(), large.size());
  ASSERT_TRUE(ServeUntilReceived({large, small}, 2));

  const augusta::link::AuthoritativeStateTally tally = client_.GetTraffic().authoritative_state;
  EXPECT_EQ(tally.updates, 2U);
  EXPECT_EQ(tally.total_bytes, large.size() + small.size());
  EXPECT_EQ(tally.last_bytes, small.size());
  EXPECT_EQ(tally.largest_bytes, large.size());
}

// Ticks one after another miss nothing; a skip counts the ticks between, by
// the gap: 10, 11, then 15 misses 12, 13 and 14.
// Requirements: US-29
TEST_F(ClientTrafficTest, ASkippedTickCountsAsMissingByTheGap) {
  ASSERT_TRUE(ServeUntilReceived({StateBytes(10), StateBytes(11), StateBytes(15)}, 3));

  const augusta::link::AuthoritativeStateTally tally = client_.GetTraffic().authoritative_state;
  EXPECT_EQ(tally.ticks_missing, 3U);
  EXPECT_EQ(tally.ticks_out_of_order, 0U);
}

// A tick before the newest seen, and the newest again, are each out of order,
// and neither moves the newest: 21 after them is one past 20, missing nothing.
// Requirements: US-29
TEST_F(ClientTrafficTest, AnOlderOrRepeatedTickCountsAsOutOfOrder) {
  ASSERT_TRUE(ServeUntilReceived({StateBytes(20), StateBytes(18), StateBytes(20), StateBytes(21)}, 4));

  const augusta::link::AuthoritativeStateTally tally = client_.GetTraffic().authoritative_state;
  EXPECT_EQ(tally.ticks_out_of_order, 2U);
  EXPECT_EQ(tally.ticks_missing, 0U);
  EXPECT_EQ(tally.updates, 4U);
}

// A Match start begins a new tick sequence: neither a tick far past the last
// Match's nor one before it counts, and the counts so far stay.
// Requirements: US-29
TEST_F(ClientTrafficTest, AMatchStartForgetsTheNewestTick) {
  const augusta::link::RawPayload start = BytesOf(augusta::link::ToWire(augusta::link::MatchStart{}));
  ASSERT_TRUE(ServeUntilReceived({StateBytes(100), StateBytes(102), start, StateBytes(500), StateBytes(501)}, 5));
  ASSERT_TRUE(ServeUntilReceived({start, StateBytes(7)}, 2));

  const augusta::link::AuthoritativeStateTally tally = client_.GetTraffic().authoritative_state;
  EXPECT_EQ(tally.ticks_missing, 1U);
  EXPECT_EQ(tally.ticks_out_of_order, 0U);
}

// A new connection forgets the newest tick with the rest of its counts, so
// its first tick counts nothing, however far from the last connection's.
// Requirements: US-29
TEST_F(ClientTrafficTest, ANewConnectionForgetsTheNewestTick) {
  ASSERT_TRUE(ServeUntilReceived({StateBytes(40)}, 1));
  client_.Disconnect();
  ASSERT_TRUE(Connect());
  ASSERT_TRUE(ServeUntilReceived({StateBytes(30), StateBytes(90)}, 2));

  const augusta::link::AuthoritativeStateTally tally = client_.GetTraffic().authoritative_state;
  EXPECT_EQ(tally.ticks_out_of_order, 0U);
  EXPECT_EQ(tally.ticks_missing, 59U);
}

}  // namespace
