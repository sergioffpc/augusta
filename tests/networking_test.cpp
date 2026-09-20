#include "augusta/networking.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

// M1 spike (ADR-0003): this is the "standalone round-trip" issue #31
// asks for - a Server and Client talking over real GameNetworkingSockets
// on loopback, in one process. It proves the transport on whichever OS
// runs it (Windows via build-client, Linux via build-server in CI); the
// literal Windows-client-to-Linux-server crossing is a manual check
// against a real second machine, since a single test process can only
// ever be one OS.
namespace {

using augusta::networking::Client;
using augusta::networking::ConnectionState;
using augusta::networking::Endpoint;
using augusta::networking::Payload;
using augusta::networking::PeerEventType;
using augusta::networking::PeerId;
using augusta::networking::PeerMessage;
using augusta::networking::Reliability;
using augusta::networking::Server;
using augusta::networking::SimulateNetworkConditions;

// How long PollUntil sleeps between polls - short enough not to add
// meaningful latency to the test, long enough not to busy-spin.
constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr auto kPollDeadline = std::chrono::seconds(5);

Payload MakePayload(const std::string& text) {
  const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
  return {bytes, bytes + text.size()};
}

std::string PayloadToString(const Payload& payload) {
  return {reinterpret_cast<const char*>(payload.data()), payload.size()};
}

// GNS's handshake and message delivery are asynchronous even on
// loopback, so this polls both sides (calling poll_both each iteration)
// until predicate is true rather than assuming a fixed number of
// PumpEvents calls is enough.
template <typename PollBoth, typename Predicate>
bool PollUntil(PollBoth poll_both, Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (std::chrono::steady_clock::now() < deadline) {
    poll_both();
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  return false;
}

// Accepts every connecting peer, recording the first one - this test
// only ever has one. Factored out of TestBody to keep its cognitive
// complexity down.
void AcceptFirstPeer(Server& server, std::optional<PeerId>& peer) {
  for (const auto& event : server.PumpEvents()) {
    if (event.type == PeerEventType::kConnectRequested) {
      peer = event.peer;
      server.Accept(event.peer);
    }
  }
}

// Polls until receive() has yielded at least one message, appending
// every batch into received as it goes. Also factored out of TestBody
// to keep its cognitive complexity down.
template <typename PollBoth, typename ReceiveFn, typename Message>
bool ReceiveAtLeastOne(PollBoth poll_both, ReceiveFn receive, std::vector<Message>& received) {
  return PollUntil(poll_both, [&] {
    std::vector<Message> messages = receive();
    received.insert(received.end(), messages.begin(), messages.end());
    return !received.empty();
  });
}

struct RoundTripResult {
  std::string received_by_server;
  std::string received_by_client;
};

// Connects, waits for the handshake, sends a payload each way, and
// waits for it to arrive on the other side. Uses plain if/return
// instead of ASSERT_* so this function's own cognitive complexity stays
// low - TestBody asserts once on the bool this returns instead of
// asserting at every step itself.
bool PerformRoundTrip(Server& server, Client& client, RoundTripResult& result) {
  std::optional<PeerId> peer;
  auto poll_both = [&] {
    client.PumpEvents();
    AcceptFirstPeer(server, peer);
  };
  if (!PollUntil(poll_both, [&] { return client.GetState() == ConnectionState::kConnected; }) || !peer.has_value()) {
    return false;
  }

  client.Send(MakePayload("hello from client"), Reliability::kUnreliable);
  std::vector<PeerMessage> received_by_server;
  if (!ReceiveAtLeastOne(poll_both, [&] { return server.ReceiveMessages(); }, received_by_server)) {
    return false;
  }
  result.received_by_server = PayloadToString(received_by_server.front().payload);

  server.Send(*peer, MakePayload("hello from server"), Reliability::kUnreliable);
  std::vector<Payload> received_by_client;
  if (!ReceiveAtLeastOne(poll_both, [&] { return client.ReceiveMessages(); }, received_by_client)) {
    return false;
  }
  result.received_by_client = PayloadToString(received_by_client.front());

  client.Disconnect();
  return true;
}

}  // namespace

// Init and Shutdown once for the whole process: a fixture's own
// SetUpTestSuite would run them once per fixture, and the transport is not
// meant to be torn down and brought back up mid-process. Without the
// Shutdown, GameNetworkingSockets' still-referenced OpenSSL state reads as a
// leak under ASan once this process exits - see networking.h's own note on
// Shutdown().
class NetworkingEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { augusta::networking::Init(); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kNetworkingEnvironment =
    ::testing::AddGlobalTestEnvironment(new NetworkingEnvironment);

class NetworkingTest : public ::testing::Test {};

TEST_F(NetworkingTest, RoundTripsAMessageBothWays) {
  Server server(Endpoint{.address = "127.0.0.1:27016"});
  Client client;
  client.Connect(Endpoint{.address = "127.0.0.1:27016"});

  RoundTripResult result;
  ASSERT_TRUE(PerformRoundTrip(server, client, result));
  EXPECT_EQ(result.received_by_server, "hello from client");
  EXPECT_EQ(result.received_by_client, "hello from server");
}

// A Server and a Client already connected to each other over loopback, for
// the tests that are about what happens to messages afterwards rather than
// about the handshake.
class ConnectedNetworkingTest : public NetworkingTest {
 protected:
  void SetUp() override {
    server_ = std::make_unique<Server>(Endpoint{.address = "127.0.0.1:27017"});
    client_ = std::make_unique<Client>();
    client_->Connect(Endpoint{.address = "127.0.0.1:27017"});
    ASSERT_TRUE(PollUntil([&] { PollBoth(); }, [&] { return client_->GetState() == ConnectionState::kConnected; }));
    ASSERT_TRUE(PollUntil([&] { PollBoth(); }, [&] { return peer_.has_value(); }));
  }

  // The conditions are process-wide, so a test that set some must not leak
  // them into the next one.
  void TearDown() override {
    SimulateNetworkConditions({});
    client_.reset();
    server_.reset();
  }

  void PollBoth() {
    client_->PumpEvents();
    AcceptFirstPeer(*server_, peer_);
  }

  // Polls both sides until done(received-so-far) is true, or the deadline
  // passes; returns whatever the server received, in arrival order.
  template <typename DoneFn>
  std::vector<std::string> ReceiveOnServerUntil(DoneFn done) {
    std::vector<std::string> received;
    PollUntil([&] { PollBoth(); },
              [&] {
                for (const PeerMessage& message : server_->ReceiveMessages()) {
                  received.push_back(PayloadToString(message.payload));
                }
                return done(received);
              });
    return received;
  }

  std::vector<std::string> ReceiveOnServer(std::size_t count) {
    return ReceiveOnServerUntil([count](const std::vector<std::string>& received) { return received.size() >= count; });
  }

  // Whatever the server receives over the next window, for asserting that
  // nothing more arrives.
  std::vector<std::string> DrainServerFor(std::chrono::milliseconds window) {
    const auto end = std::chrono::steady_clock::now() + window;
    return ReceiveOnServerUntil(
        [end](const std::vector<std::string>&) { return std::chrono::steady_clock::now() >= end; });
  }

  std::vector<std::string> ReceiveOnClient(std::size_t count) {
    std::vector<std::string> received;
    PollUntil([&] { PollBoth(); },
              [&] {
                for (const Payload& payload : client_->ReceiveMessages()) {
                  received.push_back(PayloadToString(payload));
                }
                return received.size() >= count;
              });
    return received;
  }

  std::unique_ptr<Server> server_;
  std::unique_ptr<Client> client_;
  std::optional<PeerId> peer_;
};

constexpr int kBurst = 50;
constexpr auto kDrainWindow = std::chrono::milliseconds(300);

std::vector<std::string> NumberedMessages(int count) {
  std::vector<std::string> messages;
  for (int i = 0; i < count; ++i) {
    messages.push_back("message " + std::to_string(i));
  }
  return messages;
}

TEST_F(ConnectedNetworkingTest, ReliableMessagesArriveOnceAndInOrderDespiteLoss) {
  SimulateNetworkConditions({.loss_percent = 30.0F});

  const std::vector<std::string> sent = NumberedMessages(kBurst);
  for (const std::string& text : sent) {
    client_->Send(MakePayload(text), Reliability::kReliable);
  }

  EXPECT_EQ(ReceiveOnServer(sent.size()), sent);
  // Nothing extra shows up after the last one: no duplicates.
  EXPECT_TRUE(DrainServerFor(kDrainWindow).empty());
}

TEST_F(ConnectedNetworkingTest, UnreliableMessagesCanBeLostButNeverDuplicated) {
  SimulateNetworkConditions({.loss_percent = 50.0F});

  // Loss drops whole packets, and small messages share a packet, so each one
  // is padded to fill about a packet by itself - otherwise a few lucky packets
  // could carry every message and nothing would be lost.
  constexpr int kSent = 200;
  constexpr std::size_t kMessageBytes = 1000;
  for (const std::string& text : NumberedMessages(kSent)) {
    client_->Send(MakePayload(text + std::string(kMessageBytes, '.')), Reliability::kUnreliable);
  }
  // Unreliable messages are not ordered against reliable ones, but with no
  // added latency they land well before a retransmitted reliable marker does,
  // so once the marker is in, everything that will arrive has (give or take
  // the short drain).
  client_->Send(MakePayload("marker"), Reliability::kReliable);
  std::vector<std::string> received = ReceiveOnServerUntil(
      [](const std::vector<std::string>& so_far) { return std::ranges::find(so_far, "marker") != so_far.end(); });
  const std::vector<std::string> late = DrainServerFor(kDrainWindow);
  received.insert(received.end(), late.begin(), late.end());

  const std::set<std::string> distinct(received.begin(), received.end());
  EXPECT_EQ(distinct.size(), received.size()) << "a message was delivered twice";
  EXPECT_LT(received.size(), static_cast<std::size_t>(kSent)) << "50% loss dropped nothing";
  EXPECT_TRUE(distinct.contains("marker"));
}

TEST_F(ConnectedNetworkingTest, ServerSendAndBroadcastHonorReliabilityToo) {
  SimulateNetworkConditions({.loss_percent = 30.0F});

  const std::vector<std::string> sent = NumberedMessages(kBurst);
  for (const std::string& text : sent) {
    server_->Send(*peer_, MakePayload(text), Reliability::kReliable);
  }
  server_->Broadcast(MakePayload("broadcast"), Reliability::kReliable);

  std::vector<std::string> expected = sent;
  expected.emplace_back("broadcast");
  EXPECT_EQ(ReceiveOnClient(expected.size()), expected);
}

TEST_F(ConnectedNetworkingTest, InjectedLatencyDelaysDelivery) {
  constexpr int kLatencyMs = 100;
  SimulateNetworkConditions({.latency_ms = kLatencyMs});

  const auto start = std::chrono::steady_clock::now();
  client_->Send(MakePayload("late"), Reliability::kUnreliable);
  const std::vector<std::string> received = ReceiveOnServer(1);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  ASSERT_EQ(received.size(), 1U);
  // "Roughly": a lower bound a little under the configured delay, and a loose
  // upper bound so a busy CI machine does not flake it.
  EXPECT_GE(elapsed, std::chrono::milliseconds(kLatencyMs - 20));
  EXPECT_LT(elapsed, std::chrono::milliseconds(kLatencyMs + 400));
}
