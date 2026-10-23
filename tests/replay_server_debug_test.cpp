#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "augusta/content.h"
#include "augusta/host_metrics.h"
#include "augusta/link.h"
#include "augusta/misbehaviour.h"
#include "augusta/policy.h"
#include "augusta/protocol.h"
#include "augusta/raw_link.h"
#include "augusta/replay_server.h"

// A replay server and a Development build's Debug subscribe (ADR-0051,
// ADR-0074): it serves no stream of a Match's state, since it runs no Match,
// and ignores the message. Built only into a Development build's replay server
// tests, whose replay_server_test.cpp starts the Link.
namespace {

using augusta::host::MessageType;
using augusta::host::ReplayServer;
using augusta::host::ReplayServerConfig;
using augusta::link::ConnectionState;
using augusta::link::Endpoint;
using augusta::protocol::DebugSubscribeWire;

constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr auto kPollDeadline = std::chrono::seconds(5);

// More misbehaviour than a server tolerates, with plenty to spare.
constexpr std::size_t kFlood = 3 * augusta::host::kMisbehaviourThreshold;

class ReplayServerDebugSubscribeTest : public ::testing::Test {
 protected:
  ReplayServerDebugSubscribeTest()
      : directory_(std::filesystem::temp_directory_path() /
                   ("augusta_replay_server_debug_" + std::to_string(std::random_device{}()))) {
    std::filesystem::create_directories(directory_);
    server_ = std::make_unique<ReplayServer>(ReplayServerConfig{.tick_rate_hz = 60,
                                                                .parameters = {},
                                                                .listen = Endpoint{.address = "127.0.0.1:0"},
                                                                .server_pack = {},
                                                                .captures = directory_,
                                                                .max_viewers = 1,
                                                                .transport = {}},
                                             augusta::scenario::Scenario{}, [] { return augusta::policy::Policy{}; });
  }

  ~ReplayServerDebugSubscribeTest() override {
    server_.reset();
    std::filesystem::remove_all(directory_);
  }

  ReplayServerDebugSubscribeTest(const ReplayServerDebugSubscribeTest&) = delete;
  ReplayServerDebugSubscribeTest& operator=(const ReplayServerDebugSubscribeTest&) = delete;
  ReplayServerDebugSubscribeTest(ReplayServerDebugSubscribeTest&&) = delete;
  ReplayServerDebugSubscribeTest& operator=(ReplayServerDebugSubscribeTest&&) = delete;

  // Runs the server and client until until() holds or the deadline passes;
  // returns whether it held.
  template <typename Condition>
  bool RunUntil(augusta::link::RawClient& client, Condition until) {
    const auto deadline = std::chrono::steady_clock::now() + kPollDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      server_->PumpNetwork(std::chrono::steady_clock::now());
      client.PumpEvents();
      static_cast<void>(client.Receive());
      if (until()) {
        return true;
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    return false;
  }

  std::filesystem::path directory_;
  std::unique_ptr<ReplayServer> server_;
};

// To confirm in review: the spec says only that a replay server serves
// neither of a Development build's messages.
TEST_F(ReplayServerDebugSubscribeTest, ADebugSubscribeIsIgnoredAndNeverMisbehaviour) {
  augusta::link::RawClient client;
  client.Connect({.address = server_->ListenEndpoint().address});
  ASSERT_TRUE(RunUntil(client, [&] { return client.GetState() == ConnectionState::kConnected; }));

  const auto payload = augusta::protocol::Encode(DebugSubscribeWire{.flags = 0xFF}).value();
  for (std::size_t i = 0; i < kFlood; ++i) {
    ASSERT_TRUE(client.Send(payload).has_value());
  }
  const augusta::host::HostMetrics& metrics = server_->Metrics();
  ASSERT_TRUE(
      RunUntil(client, [&] { return metrics.messages_received[MessageType::kDebugSubscribe].Value() == kFlood; }));

  EXPECT_EQ(client.GetState(), ConnectionState::kConnected);
  EXPECT_EQ(metrics.misbehaviour.Total(), 0U);
  client.Disconnect();
}

}  // namespace
