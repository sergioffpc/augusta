#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/networking.h"

// The transport's tap (ADR-0074, Development build only), over loopback: what
// a Client and a Server report of the messages they really send and receive.
// networking_test.cpp's global environment has already run Init.
namespace {

using augusta::networking::Client;
using augusta::networking::ConnectionState;
using augusta::networking::Endpoint;
using augusta::networking::Ipv4Address;
using augusta::networking::Ipv6Address;
using augusta::networking::Payload;
using augusta::networking::PeerEventType;
using augusta::networking::PeerId;
using augusta::networking::Reliability;
using augusta::networking::SendOutcome;
using augusta::networking::Server;
using augusta::networking::Tap;
using augusta::networking::TapDirection;
using augusta::networking::TapEndpoint;
using augusta::networking::TappedMessage;

constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr auto kPollDeadline = std::chrono::seconds(5);
constexpr const char* kLoopbackAnyPort = "127.0.0.1:0";
constexpr Ipv4Address kLoopback{127, 0, 0, 1};

Payload MakePayload(const std::string& text) {
  const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
  return {bytes, bytes + text.size()};
}

// One reported message, its payload copied out of the call.
struct Report {
  TapDirection direction;
  TapEndpoint local;
  TapEndpoint remote;
  std::string payload;
  std::chrono::system_clock::time_point time;
};

// Keeps every message reported to it; reported to from any thread.
class RecordingTap : public Tap {
 public:
  void Report(const TappedMessage& message) override {
    const std::lock_guard<std::mutex> lock(mutex_);
    reports_.push_back(
        ::Report{.direction = message.direction,
                 .local = message.local,
                 .remote = message.remote,
                 .payload = std::string(reinterpret_cast<const char*>(message.payload.data()), message.payload.size()),
                 .time = message.time});
  }

  [[nodiscard]] std::vector<::Report> Reports() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return reports_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<::Report> reports_;
};

template <typename PollFn, typename Predicate>
bool PollUntil(PollFn poll, Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
  while (std::chrono::steady_clock::now() < deadline) {
    poll();
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  return false;
}

std::uint16_t PortOf(const Endpoint& endpoint) {
  return static_cast<std::uint16_t>(std::stoi(endpoint.address.substr(endpoint.address.rfind(':') + 1)));
}

// A tapped Server and a tapped Client connected to it over loopback.
class NetworkingTapTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = std::make_unique<Server>(Endpoint{.address = kLoopbackAnyPort}, nullptr, &server_tap_);
    client_ = std::make_unique<Client>(nullptr, &client_tap_);
    client_->Connect(server_->LocalEndpoint());
    ASSERT_TRUE(PollUntil([&] { PollBoth(); },
                          [&] { return client_->GetState() == ConnectionState::kConnected && peer_.has_value(); }));
  }

  void PollBoth() {
    client_->PumpEvents();
    for (const auto& event : server_->PumpEvents()) {
      if (event.type == PeerEventType::kConnectRequested) {
        peer_ = event.peer;
        server_->Accept(event.peer);
      }
    }
  }

  void ReceiveOnServer() {
    ASSERT_TRUE(PollUntil([&] { PollBoth(); }, [&] { return !server_->ReceiveMessages().value().empty(); }));
  }

  void ReceiveOnClient() {
    ASSERT_TRUE(PollUntil([&] { PollBoth(); }, [&] { return !client_->ReceiveMessages().value().empty(); }));
  }

  [[nodiscard]] TapEndpoint ServerEndpoint() const {
    return TapEndpoint{.address = kLoopback, .port = PortOf(server_->LocalEndpoint())};
  }

  // Declared before the transports, so they outlive them.
  RecordingTap server_tap_;
  RecordingTap client_tap_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<Client> client_;
  std::optional<PeerId> peer_;
};

TEST_F(NetworkingTapTest, TheClientReportsWhatItSendsAndReceivesWithTheServerAsItsRemoteEnd) {
  const auto before = std::chrono::system_clock::now();
  ASSERT_EQ(client_->Send(MakePayload("ping"), Reliability::kReliable), SendOutcome::kAccepted);
  ReceiveOnServer();
  ASSERT_EQ(server_->Send(*peer_, MakePayload("pong"), Reliability::kReliable), SendOutcome::kAccepted);
  ReceiveOnClient();
  const auto after = std::chrono::system_clock::now();

  const std::vector<Report> reports = client_tap_.Reports();
  ASSERT_EQ(reports.size(), 2U);
  EXPECT_EQ(reports[0].direction, TapDirection::kOutbound);
  EXPECT_EQ(reports[0].payload, "ping");
  EXPECT_EQ(reports[1].direction, TapDirection::kInbound);
  EXPECT_EQ(reports[1].payload, "pong");
  for (const Report& report : reports) {
    EXPECT_EQ(report.local, TapEndpoint{});
    EXPECT_EQ(report.remote, ServerEndpoint());
    EXPECT_GE(report.time, before);
    EXPECT_LE(report.time, after);
  }
}

TEST_F(NetworkingTapTest, TheServerReportsWhatItSendsAndReceivesWithItsListenAddressAsItsLocalEnd) {
  ASSERT_EQ(client_->Send(MakePayload("ping"), Reliability::kReliable), SendOutcome::kAccepted);
  ReceiveOnServer();
  ASSERT_EQ(server_->Send(*peer_, MakePayload("pong"), Reliability::kReliable), SendOutcome::kAccepted);
  ReceiveOnClient();

  const std::vector<Report> reports = server_tap_.Reports();
  ASSERT_EQ(reports.size(), 2U);
  EXPECT_EQ(reports[0].direction, TapDirection::kInbound);
  EXPECT_EQ(reports[0].payload, "ping");
  EXPECT_EQ(reports[1].direction, TapDirection::kOutbound);
  EXPECT_EQ(reports[1].payload, "pong");
  for (const Report& report : reports) {
    EXPECT_EQ(report.local, ServerEndpoint());
    EXPECT_EQ(report.remote.address, (std::variant<Ipv4Address, Ipv6Address>{kLoopback}));
    EXPECT_NE(report.remote.port, 0);
    EXPECT_NE(report.remote.port, ServerEndpoint().port);
  }
}

TEST_F(NetworkingTapTest, ABroadcastIsReportedOncePerPeerThatAcceptedIt) {
  ASSERT_TRUE(server_->Broadcast(MakePayload("all"), Reliability::kReliable).has_value());
  ReceiveOnClient();

  const std::vector<Report> reports = server_tap_.Reports();
  ASSERT_EQ(reports.size(), 1U);
  EXPECT_EQ(reports[0].direction, TapDirection::kOutbound);
  EXPECT_EQ(reports[0].payload, "all");
}

// The send to the connected peer, in the same setup, is the control: it is
// reported, so the dropped ones are left out for being dropped.
TEST_F(NetworkingTapTest, ASendTheTransportDropsIsNotReported) {
  EXPECT_EQ(server_->Send(PeerId{42}, MakePayload("nobody"), Reliability::kReliable), SendOutcome::kDropped);
  ASSERT_EQ(server_->Send(*peer_, MakePayload("somebody"), Reliability::kReliable), SendOutcome::kAccepted);
  client_->Disconnect();
  EXPECT_EQ(client_->Send(MakePayload("nobody"), Reliability::kReliable), SendOutcome::kDropped);

  const std::vector<Report> server_reports = server_tap_.Reports();
  ASSERT_EQ(server_reports.size(), 1U);
  EXPECT_EQ(server_reports[0].payload, "somebody");
  EXPECT_TRUE(client_tap_.Reports().empty());
}

}  // namespace
