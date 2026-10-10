#include "augusta/networking.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include "augusta/failure.h"
#include "augusta/faults.h"

// M1 spike (ADR-0003): this is the "standalone round-trip" issue #31
// asks for - a Server and Client talking over real GameNetworkingSockets
// on loopback, in one process. It proves the transport on whichever OS
// runs it (Windows via build-client, Linux via build-server in CI); the
// literal Windows-client-to-Linux-server crossing is a manual check
// against a real second machine, since a single test process can only
// ever be one OS.
namespace {

using augusta::failure::ClassifiedFailure;
using augusta::failure::Code;
using augusta::failure::Disposition;
using augusta::failure::DispositionOf;
using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::networking::Client;
using augusta::networking::ConnectionState;
using augusta::networking::DisconnectReason;
using augusta::networking::Endpoint;
using augusta::networking::Payload;
using augusta::networking::PeerEventType;
using augusta::networking::PeerId;
using augusta::networking::PeerMessage;
using augusta::networking::PeerStats;
using augusta::networking::Reliability;
using augusta::networking::SendOutcome;
using augusta::networking::Server;
using augusta::networking::SimulateNetworkConditions;

// How long PollUntil sleeps between polls - short enough not to add
// meaningful latency to the test, long enough not to busy-spin.
constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr auto kPollDeadline = std::chrono::seconds(5);

// ctest runs every test case in its own process, possibly in parallel, so a
// fixed port would collide: each server binds port 0, a free one of its own
// choosing, and its client connects to the one the server reports.
constexpr const char* kLoopbackAnyPort = "127.0.0.1:0";

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
template <typename PollBoth, typename ReceiveFn, typename MessageWire>
bool ReceiveAtLeastOne(PollBoth poll_both, ReceiveFn receive, std::vector<MessageWire>& received) {
  return PollUntil(poll_both, [&] {
    std::vector<MessageWire> messages = receive();
    received.insert(received.end(), messages.begin(), messages.end());
    return !received.empty();
  });
}

// IPv4 UDP sockets, as many as asked for, bound to the wildcard address each on
// a port the OS picks, as a client's is; held until destroyed.
class WildcardUdpSockets {
 public:
  explicit WildcardUdpSockets(std::size_t count) {
#ifdef _WIN32
    WSADATA wsa_data;
    started_ = WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
#endif
    for (std::size_t index = 0; index < count; ++index) {
      const NativeSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (socket == kInvalidSocket) {
        return;
      }
      sockets_.push_back(socket);
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      socklen_t length = sizeof(addr);
      if (::bind(socket, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 ||
          ::getsockname(socket, reinterpret_cast<sockaddr*>(&addr), &length) != 0) {
        return;
      }
      ports_.insert(ntohs(addr.sin_port));
    }
  }

  ~WildcardUdpSockets() {
    for (const NativeSocket socket : sockets_) {
#ifdef _WIN32
      closesocket(socket);
#else
      close(socket);
#endif
    }
#ifdef _WIN32
    if (started_) {
      WSACleanup();
    }
#endif
  }

  WildcardUdpSockets(const WildcardUdpSockets&) = delete;
  WildcardUdpSockets& operator=(const WildcardUdpSockets&) = delete;
  WildcardUdpSockets(WildcardUdpSockets&&) = delete;
  WildcardUdpSockets& operator=(WildcardUdpSockets&&) = delete;

  [[nodiscard]] const std::set<std::uint16_t>& Ports() const { return ports_; }

 private:
#ifdef _WIN32
  using NativeSocket = SOCKET;
  static constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
  bool started_ = false;
#else
  using NativeSocket = int;
  static constexpr NativeSocket kInvalidSocket = -1;
#endif
  std::vector<NativeSocket> sockets_;
  std::set<std::uint16_t> ports_;
};

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

  if (client.Send(MakePayload("hello from client"), Reliability::kUnreliable) != SendOutcome::kAccepted) {
    return false;
  }
  std::vector<PeerMessage> received_by_server;
  if (!ReceiveAtLeastOne(poll_both, [&] { return server.ReceiveMessages().value(); }, received_by_server)) {
    return false;
  }
  result.received_by_server = PayloadToString(received_by_server.front().payload);

  if (server.Send(*peer, MakePayload("hello from server"), Reliability::kUnreliable) != SendOutcome::kAccepted) {
    return false;
  }
  std::vector<Payload> received_by_client;
  if (!ReceiveAtLeastOne(poll_both, [&] { return client.ReceiveMessages().value(); }, received_by_client)) {
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
  void SetUp() override { ASSERT_TRUE(augusta::networking::Init().has_value()); }
  void TearDown() override { augusta::networking::Shutdown(); }
};

[[maybe_unused]] ::testing::Environment* const kNetworkingEnvironment =
    ::testing::AddGlobalTestEnvironment(new NetworkingEnvironment);

class NetworkingTest : public ::testing::Test {};

TEST_F(NetworkingTest, AServerBoundToPortZeroReportsThePortItGot) {
  Server server(Endpoint{.address = kLoopbackAnyPort});

  EXPECT_TRUE(server.LocalEndpoint().address.starts_with("127.0.0.1:"));
  EXPECT_NE(server.LocalEndpoint().address, kLoopbackAnyPort);
}

// A client's socket is bound to the wildcard address on a port the OS picks,
// and on Windows a later bind to 127.0.0.1 on that same port succeeds and takes
// every packet sent to it: a server that took it would leave that client
// unable to hear its own server, though still heard by it (#342). So a server
// bound to port 0 must never land on a port a wildcard socket already holds -
// held here by the hundreds, so a server picking ports of its own at random
// would all but surely land on one of them.
TEST_F(NetworkingTest, AServerBoundToPortZeroNeverTakesAPortAWildcardSocketHolds) {
  constexpr std::size_t kHeldPorts = 512;
  constexpr int kServers = 128;
  const WildcardUdpSockets held(kHeldPorts);
  ASSERT_EQ(held.Ports().size(), kHeldPorts);

  for (int server_index = 0; server_index < kServers; ++server_index) {
    const Server server(Endpoint{.address = kLoopbackAnyPort});
    const std::string& address = server.LocalEndpoint().address;
    const auto port = static_cast<std::uint16_t>(std::stoi(address.substr(address.rfind(':') + 1)));
    ASSERT_FALSE(held.Ports().contains(port)) << address;
  }
}

TEST_F(NetworkingTest, RoundTripsAMessageBothWays) {
  Server server(Endpoint{.address = kLoopbackAnyPort});
  Client client;
  client.Connect(server.LocalEndpoint());

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
    server_ = std::make_unique<Server>(Endpoint{.address = kLoopbackAnyPort}, &faults_);
    client_ = std::make_unique<Client>(&faults_);
    client_->Connect(server_->LocalEndpoint());
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
                const auto messages = server_->ReceiveMessages().value();
                for (const PeerMessage& message : messages) {
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
                const auto payloads = client_->ReceiveMessages().value();
                for (const Payload& payload : payloads) {
                  received.push_back(PayloadToString(payload));
                }
                return received.size() >= count;
              });
    return received;
  }

  // Polls both sides until the server reports its peer disconnected, and says why.
  std::optional<DisconnectReason> WaitForServerToLosePeer() {
    std::optional<DisconnectReason> reason;
    PollUntil([&] { client_->PumpEvents(); },
              [&] {
                for (const auto& event : server_->PumpEvents()) {
                  if (event.type == PeerEventType::kDisconnected) {
                    reason = event.reason;
                  }
                }
                return reason.has_value();
              });
    return reason;
  }

  // Unarmed unless a test arms it; outlives both ends.
  Faults faults_;
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
    EXPECT_EQ(client_->Send(MakePayload(text), Reliability::kReliable), SendOutcome::kAccepted);
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
    EXPECT_TRUE(
        client_->Send(MakePayload(text + std::string(kMessageBytes, '.')), Reliability::kUnreliable).has_value());
  }
  // Unreliable messages are not ordered against reliable ones, but with no
  // added latency they land well before a retransmitted reliable marker does,
  // so once the marker is in, everything that will arrive has (give or take
  // the short drain).
  EXPECT_EQ(client_->Send(MakePayload("marker"), Reliability::kReliable), SendOutcome::kAccepted);
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
    EXPECT_EQ(server_->Send(*peer_, MakePayload(text), Reliability::kReliable), SendOutcome::kAccepted);
  }
  EXPECT_TRUE(server_->Broadcast(MakePayload("broadcast"), Reliability::kReliable).has_value());

  std::vector<std::string> expected = sent;
  expected.emplace_back("broadcast");
  EXPECT_EQ(ReceiveOnClient(expected.size()), expected);
}

TEST_F(ConnectedNetworkingTest, InjectedLatencyDelaysDelivery) {
  constexpr int kLatencyMs = 100;
  SimulateNetworkConditions({.latency_ms = kLatencyMs});

  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(client_->Send(MakePayload("late"), Reliability::kUnreliable), SendOutcome::kAccepted);
  const std::vector<std::string> received = ReceiveOnServer(1);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  ASSERT_EQ(received.size(), 1U);
  // "Roughly": a lower bound a little under the configured delay, and a loose
  // upper bound so a busy CI machine does not flake it.
  EXPECT_GE(elapsed, std::chrono::milliseconds(kLatencyMs - 20));
  EXPECT_LT(elapsed, std::chrono::milliseconds(kLatencyMs + 400));
}

TEST_F(ConnectedNetworkingTest, InjectedJitterDelaysSomeMessagesMoreThanOthers) {
  constexpr int kJitterMeanMs = 50;
  constexpr int kJitterMaxMs = 100;
  SimulateNetworkConditions({.jitter_mean_ms = kJitterMeanMs, .jitter_max_ms = kJitterMaxMs});

  // One message at a time, each timed on its own. Over loopback alone each
  // takes about a poll; with the jitter about half take longer than 40 ms.
  constexpr int kMessages = 20;
  std::chrono::steady_clock::duration longest{};
  for (int i = 0; i < kMessages; ++i) {
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(client_->Send(MakePayload("jittered"), Reliability::kUnreliable), SendOutcome::kAccepted);
    ASSERT_EQ(ReceiveOnServer(1).size(), 1U);
    longest = std::max(longest, std::chrono::steady_clock::now() - start);
  }

  EXPECT_GE(longest, std::chrono::milliseconds(40));
  // A loose upper bound so a busy CI machine does not flake it.
  EXPECT_LT(longest, std::chrono::milliseconds(kJitterMaxMs + 400));
}

// For the conditions that change the order packets arrive in.
class ReorderingNetworkingTest : public ConnectedNetworkingTest {
 protected:
  // What fills about a packet with one message (see UnreliableMessagesCanBeLostButNeverDuplicated).
  static constexpr std::size_t kPaddingBytes = 1000;

  // Sends kBurst numbered unreliable messages, each padded to fill about a
  // packet by itself, and returns where each one that arrived stood among
  // them, in arrival order.
  std::vector<int> SendPaddedBurst() {
    for (int i = 0; i < kBurst; ++i) {
      EXPECT_TRUE(
          client_->Send(MakePayload(std::to_string(i) + std::string(kPaddingBytes, '.')), Reliability::kUnreliable)
              .has_value());
    }
    std::vector<int> positions;
    for (const std::string& text : DrainServerFor(kDrainWindow)) {
      positions.push_back(std::stoi(text));
    }
    return positions;
  }
};

TEST_F(ReorderingNetworkingTest, InjectedJitterAloneKeepsMessagesInOrder) {
  SimulateNetworkConditions({.jitter_mean_ms = 20, .jitter_max_ms = 60});

  const std::vector<int> positions = SendPaddedBurst();

  ASSERT_FALSE(positions.empty());
  EXPECT_TRUE(std::ranges::is_sorted(positions));
}

TEST_F(ReorderingNetworkingTest, InjectedReorderingDeliversUnreliableMessagesOutOfOrder) {
  SimulateNetworkConditions({.reorder_percent = 50.0F, .reorder_delay_ms = 50});

  const std::vector<int> positions = SendPaddedBurst();

  ASSERT_FALSE(positions.empty());
  EXPECT_FALSE(std::ranges::is_sorted(positions));
}

TEST_F(ReorderingNetworkingTest, ReliableMessagesArriveOnceAndInOrderDespiteReordering) {
  SimulateNetworkConditions({.reorder_percent = 50.0F, .reorder_delay_ms = 50});

  // Padded to a packet each, as SendPaddedBurst's are, so packets are reordered under them.
  std::vector<std::string> sent = NumberedMessages(kBurst);
  for (std::string& text : sent) {
    text += std::string(kPaddingBytes, '.');
    EXPECT_EQ(client_->Send(MakePayload(text), Reliability::kReliable), SendOutcome::kAccepted);
  }

  EXPECT_EQ(ReceiveOnServer(sent.size()), sent);
}

TEST_F(ConnectedNetworkingTest, AClientThatClosesItsConnectionIsReportedAsClosedByPeer) {
  client_->Disconnect();

  const std::optional<DisconnectReason> reason = WaitForServerToLosePeer();

  EXPECT_EQ(reason, DisconnectReason::kClosedByPeer);
}

// What the server sent reliably before closing is the client's to receive
// even once it has seen the close: a reply that explains it, or the last of a
// Replay, is not lost with the connection.
TEST_F(ConnectedNetworkingTest, WhatArrivedBeforeTheServerClosedIsStillReceivedAfterTheClose) {
  const std::vector<std::string> sent = NumberedMessages(kBurst);
  for (const std::string& message : sent) {
    ASSERT_EQ(server_->Send(*peer_, MakePayload(message), Reliability::kReliable), SendOutcome::kAccepted);
  }
  server_->Disconnect(*peer_);
  ASSERT_TRUE(
      PollUntil([&] { client_->PumpEvents(); }, [&] { return client_->GetState() == ConnectionState::kDisconnected; }));

  std::vector<std::string> received;
  for (const Payload& payload : client_->ReceiveMessages().value()) {
    received.push_back(PayloadToString(payload));
  }

  EXPECT_EQ(received, sent);
  EXPECT_TRUE(client_->ReceiveMessages().value().empty());
}

TEST_F(ConnectedNetworkingTest, TheServerReportsTheStatsOfEachConnectedPeer) {
  std::vector<PeerStats> stats;
  ASSERT_TRUE(PollUntil([&] { PollBoth(); },
                        [&] {
                          stats = server_->GetStats();
                          return !stats.empty();
                        }));

  ASSERT_EQ(stats.size(), 1U);
  EXPECT_EQ(stats.front().peer, *peer_);
  EXPECT_GE(stats.front().stats.ping_ms, 0);
}

TEST_F(ConnectedNetworkingTest, TheServerReportsNoStatsForAPeerThatLeft) {
  client_->Disconnect();
  ASSERT_TRUE(WaitForServerToLosePeer().has_value());

  EXPECT_TRUE(server_->GetStats().empty());
}

TEST_F(NetworkingTest, APeerThatGoesSilentIsReportedAsALostConnectionOnceTheTimeoutPasses) {
  constexpr int kTimeoutMs = 500;
  // Set before the connection exists: the timeout only reaches new connections.
  SimulateNetworkConditions({.timeout_ms = kTimeoutMs});
  Server server(Endpoint{.address = kLoopbackAnyPort});
  Client client;
  client.Connect(server.LocalEndpoint());
  std::optional<PeerId> peer;
  const auto poll_both = [&] {
    client.PumpEvents();
    AcceptFirstPeer(server, peer);
  };
  ASSERT_TRUE(PollUntil(poll_both, [&] { return client.GetState() == ConnectionState::kConnected; }));

  SimulateNetworkConditions({.loss_percent = 100.0F, .timeout_ms = kTimeoutMs});
  std::optional<DisconnectReason> reason;
  // The transport notices a silent peer by the replies it stops getting, so
  // each side keeps sending the other something to answer, as a match does.
  PollUntil(
      [&] {
        client.PumpEvents();
        // Dropped once the connection ends; never a local failure.
        EXPECT_TRUE(client.Send(MakePayload("anyone there?"), Reliability::kReliable).has_value());
        EXPECT_TRUE(server.Send(*peer, MakePayload("anyone there?"), Reliability::kReliable).has_value());
      },
      [&] {
        for (const auto& event : server.PumpEvents()) {
          if (event.type == PeerEventType::kDisconnected) {
            reason = event.reason;
          }
        }
        return reason.has_value();
      });
  // Each side times out on its own clock, so the client can learn of it a poll
  // or two after the server did: keep pumping it until it does.
  const bool client_disconnected =
      PollUntil([&] { client.PumpEvents(); }, [&] { return client.GetState() == ConnectionState::kDisconnected; });
  SimulateNetworkConditions({});

  EXPECT_EQ(reason, DisconnectReason::kConnectionLost);
  EXPECT_TRUE(client_disconnected);
}

TEST_F(NetworkingTest, TheDefaultTimeoutIsBackOnceTheConditionsAreReset) {
  SimulateNetworkConditions({.timeout_ms = 200});
  SimulateNetworkConditions({});
  Server server(Endpoint{.address = kLoopbackAnyPort});
  Client client;
  client.Connect(server.LocalEndpoint());
  std::optional<PeerId> peer;
  const auto poll_both = [&] {
    client.PumpEvents();
    AcceptFirstPeer(server, peer);
  };
  ASSERT_TRUE(PollUntil(poll_both, [&] { return client.GetState() == ConnectionState::kConnected; }));

  // Were the timeout left at 200 ms or cleared to 0, a quiet moment would end the connection.
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  poll_both();

  EXPECT_EQ(client.GetState(), ConnectionState::kConnected);
}

// ---- Local transport failures (ADR-0033) ----

// A transport the process cannot start ends it before any runtime exists.
TEST_F(NetworkingTest, ATransportThatCannotBeInitializedIsATransportInitFailure) {
  Faults faults;
  faults.Arm(Site::kDependencyInit, "GameNetworkingSockets_Init failed");

  const auto initialized = augusta::networking::Init(&faults);

  ASSERT_FALSE(initialized.has_value());
  EXPECT_EQ(initialized.error().code, Code::kTransportInitFailed);
  EXPECT_EQ(initialized.error().detail, "GameNetworkingSockets_Init failed");
}

// A transport that cannot listen is the runtime's failure, typed, so the
// application boundary classifies it by its code and never by its message.
TEST_F(NetworkingTest, AServerThatCannotSetUpItsListenerThrowsAListenerSetupFailure) {
  Faults faults;
  faults.Arm(Site::kListenerSetup, "no socket");

  try {
    const Server server(Endpoint{.address = kLoopbackAnyPort}, &faults);
    FAIL() << "listening succeeded on a failed listener setup";
  } catch (const ClassifiedFailure& error) {
    EXPECT_EQ(error.GetFailure().code, Code::kListenerSetupFailed);
    EXPECT_EQ(DispositionOf(error.GetFailure().code), Disposition::kRuntime);
    EXPECT_EQ(error.GetFailure().detail, "no socket");
  }
}

TEST_F(NetworkingTest, AServerAddressThatDoesNotParseIsAConfigurationFailure) {
  try {
    const Server server(Endpoint{.address = "not an address"});
    FAIL() << "listening succeeded on an address that does not parse";
  } catch (const ClassifiedFailure& error) {
    EXPECT_EQ(error.GetFailure().code, Code::kInvalidConfiguration);
  }
}

TEST_F(NetworkingTest, AClientAddressThatDoesNotParseIsAConfigurationFailure) {
  Client client;
  try {
    client.Connect(Endpoint{.address = "not an address"});
    FAIL() << "connecting began to an address that does not parse";
  } catch (const ClassifiedFailure& error) {
    EXPECT_EQ(error.GetFailure().code, Code::kInvalidConfiguration);
  }
}

// A peer that is not there is the peer's outcome: dropped, never a failure,
// and never mistaken for a message sent.
TEST_F(NetworkingTest, SendingToNoConnectedPeerDropsTheMessageWithoutFailing) {
  Server server(Endpoint{.address = kLoopbackAnyPort});
  Client client;

  EXPECT_EQ(server.Send(PeerId{42}, MakePayload("nobody"), Reliability::kReliable), SendOutcome::kDropped);
  EXPECT_EQ(client.Send(MakePayload("nobody"), Reliability::kReliable), SendOutcome::kDropped);
}

TEST_F(NetworkingTest, NothingToReceiveIsAnEmptyQueueNotAFailure) {
  Server server(Endpoint{.address = kLoopbackAnyPort});
  Client client;

  const auto server_received = server.ReceiveMessages();
  const auto client_received = client.ReceiveMessages();

  ASSERT_TRUE(server_received.has_value());
  EXPECT_TRUE(server_received->empty());
  ASSERT_TRUE(client_received.has_value());
  EXPECT_TRUE(client_received->empty());
}

TEST_F(ConnectedNetworkingTest, AClientSendTheLocalTransportRefusesIsAFailureAndNeverArrives) {
  faults_.Arm(Site::kTransportSend, "socket closed");

  const auto sent = client_->Send(MakePayload("lost"), Reliability::kReliable);

  ASSERT_FALSE(sent.has_value());
  EXPECT_EQ(sent.error().code, Code::kTransportSendFailed);
  EXPECT_EQ(DispositionOf(sent.error().code), Disposition::kRuntime);
  EXPECT_EQ(sent.error().detail, "socket closed");
  EXPECT_TRUE(DrainServerFor(kDrainWindow).empty());
}

TEST_F(ConnectedNetworkingTest, AServerSendTheLocalTransportRefusesIsAFailureOfThatPeersSend) {
  faults_.Arm(Site::kTransportSend, "socket closed");

  const auto sent = server_->Send(*peer_, MakePayload("lost"), Reliability::kReliable);

  ASSERT_FALSE(sent.has_value());
  EXPECT_EQ(sent.error().code, Code::kTransportSendFailed);
  EXPECT_EQ(sent.error().detail, "socket closed");
  const std::string peer = std::to_string(static_cast<std::uint32_t>(*peer_));
  EXPECT_TRUE(std::ranges::any_of(sent.error().context,
                                  [&](const auto& field) { return field.key == "peer" && field.value == peer; }));
}

TEST_F(ConnectedNetworkingTest, ABroadcastStopsAtTheLocalTransportsFailure) {
  faults_.Arm(Site::kTransportSend, "socket closed");

  const auto broadcast = server_->Broadcast(MakePayload("lost"), Reliability::kReliable);

  ASSERT_FALSE(broadcast.has_value());
  EXPECT_EQ(broadcast.error().code, Code::kTransportSendFailed);
}

TEST_F(ConnectedNetworkingTest, AReceiveTheLocalTransportFailsIsAFailureNotAnEmptyQueue) {
  faults_.Arm(Site::kTransportReceive, "poll group gone", 2);

  const auto server_received = server_->ReceiveMessages();
  const auto client_received = client_->ReceiveMessages();

  ASSERT_FALSE(server_received.has_value());
  EXPECT_EQ(server_received.error().code, Code::kTransportReceiveFailed);
  EXPECT_EQ(DispositionOf(server_received.error().code), Disposition::kRuntime);
  EXPECT_EQ(server_received.error().detail, "poll group gone");
  ASSERT_FALSE(client_received.has_value());
  EXPECT_EQ(client_received.error().code, Code::kTransportReceiveFailed);
}

// The peer leaving is its own outcome: what is sent to it afterwards is
// dropped, not a failure of the server's transport.
TEST_F(ConnectedNetworkingTest, SendingToAPeerThatLeftDropsTheMessageWithoutFailing) {
  client_->Disconnect();
  ASSERT_TRUE(WaitForServerToLosePeer().has_value());

  EXPECT_EQ(server_->Send(*peer_, MakePayload("gone"), Reliability::kReliable), SendOutcome::kDropped);
  const auto broadcast = server_->Broadcast(MakePayload("gone"), Reliability::kReliable);
  EXPECT_TRUE(broadcast.has_value());
}
