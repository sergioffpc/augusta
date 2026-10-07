#ifndef AUGUSTA_SWARM_TESTS_EXAMPLE_SERVER_H_
#define AUGUSTA_SWARM_TESTS_EXAMPLE_SERVER_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/map.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "content.h"
#include "host.h"
#include "run.h"

/// \file
/// What the tests of whole runs of Scripted players (ADR-0013) share: a
/// server::Host serving the example scenario in the same process, at the tick
/// rate on a thread of its own, the RunConfig of Scripted players against it,
/// and the transport's process-wide setup.
namespace augusta::swarm::testing {

inline constexpr std::uint8_t kTickRate = 60;
inline constexpr const char* kCharacter = "soldier";

// The transport set up once for the whole test process, and torn down after:
// see networking::Shutdown. Each test file registers one.
class NetworkingEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { networking::Init(); }
  void TearDown() override { networking::Shutdown(); }
};

// The example scenario's packs, verified with the test key, as augustad and
// augusta-swarm load them.
inline assets::Pack ExamplePack(std::string_view name) {
  const std::filesystem::path root(AUGUSTA_EXAMPLE_PACKS);
  auto pack = assets::LoadVerifiedPack(root / name, root / "test.pub");
  EXPECT_TRUE(pack.has_value());
  return *std::move(pack);
}

// A Host run at the tick rate on a thread of its own, as augustad's would be,
// until it is destroyed.
class RunningHost {
 public:
  RunningHost(const server::HostConfig& config, server::Content content)
      : host_(config, std::move(content.scenario), std::move(content.policy)),
        thread_([this](const std::stop_token& stop) { Serve(stop); }) {}

  [[nodiscard]] networking::Endpoint ListenEndpoint() const { return host_.ListenEndpoint(); }

 private:
  void Serve(const std::stop_token& stop) {
    constexpr float kFixedTick = 1.0F / kTickRate;
    const auto duration =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(kFixedTick));
    auto next_tick = std::chrono::steady_clock::now();
    while (!stop.stop_requested()) {
      host_.PumpNetwork(std::chrono::steady_clock::now());
      static_cast<void>(host_.Tick(kFixedTick));
      next_tick += duration;
      std::this_thread::sleep_until(next_tick);
    }
  }

  server::Host host_;
  // Last, so it is joined before the Host goes.
  std::jthread thread_;
};

// The example scenario served for player_count, on a free loopback port. The
// example lets one player run it alone, and a Match of one never ends by last
// player standing: the server runs it for more, as a scenario cooked for them
// would, and tells each Scripted player so when it joins.
class ExampleServer {
 public:
  explicit ExampleServer(std::uint8_t player_count) {
    const assets::Pack server_pack = ExamplePack("server.pack");
    auto content = server::LoadServerContent(server_pack, kTickRate);
    if (!content.has_value()) {
      ADD_FAILURE() << server::DescribeContentError(content.error());
      return;
    }
    parameters::Parameters parameters = content->parameters;
    parameters.player_count = player_count;
    host_.emplace(server::HostConfig{.tick_rate_hz = kTickRate,
                                     .parameters = parameters,
                                     .listen = {.address = "127.0.0.1:0"},
                                     .recording = {},
                                     .server_pack = {}},
                  *std::move(content));
  }

  // A run of Scripted players against it through the example's client pack,
  // every one to see matches Match ends within timeout.
  [[nodiscard]] RunConfig RunOf(std::uint32_t matches, std::chrono::steady_clock::duration timeout) const {
    const assets::Pack client_pack = ExamplePack("client.pack");
    auto map = map::LoadCollision(client_pack);
    EXPECT_TRUE(map.has_value()) << map::DescribeMapError(map.error());
    return RunConfig{
        .session = {.server = host_.has_value() ? host_->ListenEndpoint() : networking::Endpoint{},
                    .client_pack = client_pack.Hash(),
                    .character = kCharacter},
        .map = map.has_value() ? *std::move(map) : decltype(RunConfig::map){},
        .matches = matches,
        .timeout = timeout,
        .seed = 1,
    };
  }

 private:
  std::optional<RunningHost> host_;
};

}  // namespace augusta::swarm::testing

#endif  // AUGUSTA_SWARM_TESTS_EXAMPLE_SERVER_H_
