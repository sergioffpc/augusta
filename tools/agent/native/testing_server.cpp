#include "testing_server.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

#include "augusta/assets.h"
#include "augusta/failure.h"
#include "augusta/parameters.h"
#include "content.h"
#include "host.h"

namespace augusta::agent::testing {

namespace {

constexpr std::uint8_t kTickRate = 60;

server::Content LoadContent(const std::filesystem::path& pack_path, const std::filesystem::path& public_key_path) {
  auto pack = assets::LoadVerifiedPack(pack_path, public_key_path);
  if (!pack) {
    throw std::runtime_error(assets::DescribeVerifiedPackError(pack.error(), pack_path, public_key_path));
  }
  auto content = server::LoadServerContent(*pack, kTickRate);
  if (!content) {
    throw std::runtime_error(failure::DescribeFailure(content.error()));
  }
  return *std::move(content);
}

server::HostConfig ConfigFor(const server::Content& content, std::uint8_t player_count) {
  parameters::Parameters parameters = content.parameters;
  parameters.player_count = player_count;
  return server::HostConfig{.tick_rate_hz = kTickRate,
                            .parameters = parameters,
                            .listen = {.address = "127.0.0.1:0"},
                            .server_pack = {},
                            .capture_directory = {},
                            .capture_mode = {},
                            .faults = nullptr};
}

}  // namespace

Server::Server(const std::filesystem::path& pack_path, const std::filesystem::path& public_key_path,
               std::uint8_t player_count)
    : Server(LoadContent(pack_path, public_key_path), player_count) {}

Server::Server(server::Content content, std::uint8_t player_count)
    : host_(ConfigFor(content, player_count), std::move(content.scenario), std::move(content.policy)),
      thread_([this](const std::stop_token& stop) { Serve(stop); }) {}

std::string Server::Address() const { return host_.ListenEndpoint().address; }

void Server::Serve(const std::stop_token& stop) {
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

}  // namespace augusta::agent::testing
