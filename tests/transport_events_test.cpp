#include "transport_events.h"

#include <algorithm>
#include <cstdint>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/networking.h"

// The decision half of the transport's connection events, apart from a live
// connection: what a status change does to the peers or to the client's
// connection, and which transport call the Network I/O owner makes for it.
namespace {

using augusta::networking::ApplyToClient;
using augusta::networking::ConnectionState;
using augusta::networking::DisconnectReason;
using augusta::networking::PeerEvent;
using augusta::networking::PeerEventType;
using augusta::networking::PeerId;
using augusta::networking::PeerTable;
using augusta::networking::TransportCall;
using augusta::networking::TransportEvent;
using augusta::networking::TransportEventQueue;
using augusta::networking::TransportState;

constexpr std::uint32_t kPeer = 7;
constexpr std::uint32_t kOther = 9;

TransportEvent Event(std::uint32_t connection, TransportState state) {
  return TransportEvent{.connection = connection, .state = state, .remote_address = "127.0.0.1:1"};
}

// Brings peers to a state a test starts from: the call it asks for is not what is tested.
void Feed(PeerTable& peers, std::uint32_t connection, TransportState state) {
  static_cast<void>(peers.Apply(Event(connection, state)));
}

// Events a test has already looked past.
void Discard(const std::vector<PeerEvent>& /*events*/) {}

std::vector<PeerEventType> Types(const std::vector<PeerEvent>& events) {
  std::vector<PeerEventType> types;
  types.reserve(events.size());
  for (const PeerEvent& event : events) {
    types.push_back(event.type);
  }
  return types;
}

TEST(TransportEventQueueTest, HandsOutWhatWasPublishedInOrderOnce) {
  TransportEventQueue queue;
  queue.Publish(Event(kPeer, TransportState::kConnecting));
  queue.Publish(Event(kPeer, TransportState::kConnected));

  const std::vector<TransportEvent> drained = queue.Drain();

  ASSERT_EQ(drained.size(), 2U);
  EXPECT_EQ(drained[0].state, TransportState::kConnecting);
  EXPECT_EQ(drained[1].state, TransportState::kConnected);
  EXPECT_TRUE(queue.Drain().empty());
}

TEST(TransportEventQueueTest, TakesEventsPublishedFromAnotherThread) {
  constexpr std::uint32_t kEvents = 100;
  TransportEventQueue queue;
  std::thread publisher([&] {
    for (std::uint32_t i = 0; i < kEvents; ++i) {
      queue.Publish(Event(i, TransportState::kConnecting));
    }
  });
  publisher.join();

  EXPECT_EQ(queue.Drain().size(), kEvents);
}

TEST(PeerTableTest, AConnectingPeerIsRequestedUntilAnswered) {
  PeerTable peers;
  EXPECT_EQ(peers.Apply(Event(kPeer, TransportState::kConnecting)), TransportCall::kNone);

  EXPECT_EQ(Types(peers.TakeEvents()), std::vector{PeerEventType::kConnectRequested});
  EXPECT_EQ(Types(peers.TakeEvents()), std::vector{PeerEventType::kConnectRequested});

  peers.Answer(kPeer);
  EXPECT_TRUE(peers.TakeEvents().empty());
}

TEST(PeerTableTest, AConnectedPeerJoinsThePollGroupAndIsReportedOnce) {
  PeerTable peers;
  Feed(peers, kPeer, TransportState::kConnecting);
  peers.Answer(kPeer);

  EXPECT_EQ(peers.Apply(Event(kPeer, TransportState::kConnected)), TransportCall::kJoinPollGroup);

  const std::vector<PeerEvent> events = peers.TakeEvents();
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].type, PeerEventType::kConnected);
  EXPECT_EQ(events[0].peer, static_cast<PeerId>(kPeer));
  EXPECT_TRUE(peers.IsConnected(kPeer));
  EXPECT_TRUE(peers.TakeEvents().empty());
}

TEST(PeerTableTest, APeerThatClosesIsForgottenClosedAndReportedAsClosedByPeer) {
  PeerTable peers;
  Feed(peers, kPeer, TransportState::kConnected);
  Discard(peers.TakeEvents());

  EXPECT_EQ(peers.Apply(Event(kPeer, TransportState::kClosedByPeer)), TransportCall::kClose);

  const std::vector<PeerEvent> events = peers.TakeEvents();
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].type, PeerEventType::kDisconnected);
  EXPECT_EQ(events[0].reason, DisconnectReason::kClosedByPeer);
  EXPECT_FALSE(peers.IsConnected(kPeer));
  EXPECT_TRUE(peers.Connections().empty());
}

TEST(PeerTableTest, APeerTheTransportGaveUpOnIsReportedAsLost) {
  PeerTable peers;
  Feed(peers, kPeer, TransportState::kConnected);
  Discard(peers.TakeEvents());

  EXPECT_EQ(peers.Apply(Event(kPeer, TransportState::kProblemDetectedLocally)), TransportCall::kClose);

  const std::vector<PeerEvent> events = peers.TakeEvents();
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].reason, DisconnectReason::kConnectionLost);
}

TEST(PeerTableTest, APendingPeerThatDropsIsNoLongerRequested) {
  PeerTable peers;
  Feed(peers, kPeer, TransportState::kConnecting);

  EXPECT_EQ(peers.Apply(Event(kPeer, TransportState::kClosedByPeer)), TransportCall::kClose);

  EXPECT_EQ(Types(peers.TakeEvents()), std::vector{PeerEventType::kDisconnected});
  EXPECT_TRUE(peers.Connections().empty());
}

TEST(PeerTableTest, AStateItDoesNotActOnChangesNothing) {
  PeerTable peers;
  Feed(peers, kPeer, TransportState::kConnected);
  Discard(peers.TakeEvents());

  EXPECT_EQ(peers.Apply(Event(kPeer, TransportState::kOther)), TransportCall::kNone);

  EXPECT_TRUE(peers.TakeEvents().empty());
  EXPECT_TRUE(peers.IsConnected(kPeer));
}

TEST(PeerTableTest, ForgettingAPeerStopsItBeingConnected) {
  PeerTable peers;
  Feed(peers, kPeer, TransportState::kConnected);
  Feed(peers, kOther, TransportState::kConnecting);

  peers.Forget(kPeer);

  EXPECT_FALSE(peers.IsConnected(kPeer));
  EXPECT_EQ(peers.Connections(), std::vector<std::uint32_t>{kOther});
}

// A connection this side ends because the transport cannot keep it is a peer
// outcome: reported like any departure, so its Session ends and nothing else.
TEST(PeerTableTest, APeerTheTransportCannotKeepIsClosedAndReportedAsLost) {
  PeerTable peers;
  Feed(peers, kPeer, TransportState::kConnected);
  Feed(peers, kOther, TransportState::kConnected);
  Discard(peers.TakeEvents());

  EXPECT_EQ(peers.Lose(kPeer), TransportCall::kClose);

  const std::vector<PeerEvent> events = peers.TakeEvents();
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].peer, PeerId{kPeer});
  EXPECT_EQ(events[0].type, PeerEventType::kDisconnected);
  EXPECT_EQ(events[0].reason, DisconnectReason::kConnectionLost);
  EXPECT_FALSE(peers.IsConnected(kPeer));
  EXPECT_TRUE(peers.IsConnected(kOther));
}

TEST(PeerTableTest, LosingAPeerAlreadyGoneChangesNothing) {
  PeerTable peers;

  EXPECT_EQ(peers.Lose(kPeer), TransportCall::kNone);

  EXPECT_TRUE(peers.TakeEvents().empty());
}

TEST(PeerTableTest, ConnectionsHoldsBothPendingAndConnectedPeers) {
  PeerTable peers;
  Feed(peers, kPeer, TransportState::kConnected);
  Feed(peers, kOther, TransportState::kConnecting);

  std::vector<std::uint32_t> connections = peers.Connections();
  std::ranges::sort(connections);

  EXPECT_EQ(connections, (std::vector<std::uint32_t>{kPeer, kOther}));
}

TEST(ApplyToClientTest, TheCurrentConnectionConnectingAndConnectedMoveTheState) {
  EXPECT_EQ(ApplyToClient(kPeer, ConnectionState::kConnecting, Event(kPeer, TransportState::kConnecting)).state,
            ConnectionState::kConnecting);

  const auto connected = ApplyToClient(kPeer, ConnectionState::kConnecting, Event(kPeer, TransportState::kConnected));

  EXPECT_EQ(connected.state, ConnectionState::kConnected);
  EXPECT_EQ(connected.call, TransportCall::kNone);
  EXPECT_FALSE(connected.ended);
}

TEST(ApplyToClientTest, TheCurrentConnectionEndingDisconnectsAndClosesIt) {
  for (const TransportState state : {TransportState::kClosedByPeer, TransportState::kProblemDetectedLocally}) {
    const auto ended = ApplyToClient(kPeer, ConnectionState::kConnected, Event(kPeer, state));

    EXPECT_EQ(ended.state, ConnectionState::kDisconnected);
    EXPECT_EQ(ended.call, TransportCall::kClose);
    EXPECT_TRUE(ended.ended);
  }
}

TEST(ApplyToClientTest, AnEarlierConnectionEndingIsClosedWithoutTouchingTheState) {
  const auto stale = ApplyToClient(kPeer, ConnectionState::kConnected, Event(kOther, TransportState::kClosedByPeer));

  EXPECT_EQ(stale.state, ConnectionState::kConnected);
  EXPECT_EQ(stale.call, TransportCall::kClose);
  EXPECT_FALSE(stale.ended);
}

TEST(ApplyToClientTest, AnEarlierConnectionConnectingChangesNothing) {
  const auto stale = ApplyToClient(kPeer, ConnectionState::kConnecting, Event(kOther, TransportState::kConnected));

  EXPECT_EQ(stale.state, ConnectionState::kConnecting);
  EXPECT_EQ(stale.call, TransportCall::kNone);
}

}  // namespace
