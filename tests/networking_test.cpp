#include "augusta/networking.h"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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
using augusta::networking::Server;

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

  client.Send(MakePayload("hello from client"));
  std::vector<PeerMessage> received_by_server;
  if (!ReceiveAtLeastOne(poll_both, [&] { return server.ReceiveMessages(); }, received_by_server)) {
    return false;
  }
  result.received_by_server = PayloadToString(received_by_server.front().payload);

  server.Send(*peer, MakePayload("hello from server"));
  std::vector<Payload> received_by_client;
  if (!ReceiveAtLeastOne(poll_both, [&] { return client.ReceiveMessages(); }, received_by_client)) {
    return false;
  }
  result.received_by_client = PayloadToString(received_by_client.front());

  client.Disconnect();
  return true;
}

}  // namespace

class NetworkingTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { augusta::networking::Init(); }
};

TEST_F(NetworkingTest, RoundTripsAMessageBothWays) {
  Server server(Endpoint{.address = "127.0.0.1:27016"});
  Client client;
  client.Connect(Endpoint{.address = "127.0.0.1:27016"});

  RoundTripResult result;
  ASSERT_TRUE(PerformRoundTrip(server, client, result));
  EXPECT_EQ(result.received_by_server, "hello from client");
  EXPECT_EQ(result.received_by_client, "hello from server");
}
