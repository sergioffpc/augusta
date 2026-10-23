#include "augusta/client_application.h"

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/uuid/uuid.hpp>

#include "augusta/application.h"
#include "augusta/assets.h"
#include "augusta/client.h"
#include "augusta/client_config.h"
#include "augusta/client_phases.h"
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/link.h"
#include "augusta/reenactment.h"

namespace augusta::client {

namespace {

// How long `augustac --replays` waits for the replay server's list.
constexpr auto kReplayListPatience = std::chrono::seconds(10);
// How often it pumps the connection while it waits.
constexpr auto kReplayListPollInterval = std::chrono::milliseconds(10);

application::Outcome QueryReplays(const config::ClientConfig& file_config) {
  if (auto initialized = link::Init(); !initialized) {
    return std::move(initialized.error());
  }
  harness::ReplayListQuery query(link::Endpoint{.address = file_config.server_address});
  const auto deadline = std::chrono::steady_clock::now() + kReplayListPatience;
  while (!query.List().has_value() && !query.GetFailure().has_value() && std::chrono::steady_clock::now() < deadline) {
    query.Pump();
    if (std::optional<failure::Failure> failed = query.TakeTransportFailure()) {
      return std::move(*failed);
    }
    std::this_thread::sleep_for(kReplayListPollInterval);
  }
  if (const std::optional<std::vector<harness::ReplayListing>>& list = query.List()) {
    std::println("{}", harness::DescribeReplayList(*list));
    return std::nullopt;
  }
  return ClassifySessionFailure(
      query.GetFailure().value_or(harness::Failure{.kind = harness::FailureKind::kServerUnreachable, .refusal = {}}));
}

}  // namespace

// Declared first, destroyed last: the pack outlives the runtime that reads from
// it.
struct ClientProcess::Parts {
  std::unique_ptr<const assets::Pack> pack;
  std::unique_ptr<ClientRuntime> runtime;
};

ClientProcess::ClientProcess(std::unique_ptr<Parts> parts) : parts_(std::move(parts)) {}

ClientProcess::~ClientProcess() = default;

application::Outcome ClientProcess::Run() { return parts_->runtime->Run(); }

namespace {

// Loads what the runtime is made from and constructs it (ClientLifecycle).
std::expected<std::unique_ptr<ClientProcess>, failure::Failure> ConstructProcess(
    const config::ClientConfig& file_config, const std::optional<harness::Script>& reenactment,
    const std::optional<std::string>& replay, boost::uuids::uuid run) {
  auto loaded = LoadClient(file_config);
  if (!loaded) {
    return std::unexpected(std::move(loaded.error()));
  }
  if (reenactment.has_value()) {
    if (auto fits = CheckReenactmentPack(*reenactment, loaded->pack->Hash()); !fits) {
      return std::unexpected(std::move(fits.error()));
    }
  }

  const RuntimeConfig config{
      .input = file_config.input,
      .server_address = file_config.server_address,
      .character = file_config.character,
      .client_pack = loaded->pack->Hash(),
      .reenactment = reenactment,
      .replay = replay,
      .run = run,
#if AUGUSTA_DEBUG_TOOLS
      .debug_hud = file_config.debug.hud,
      .debug_profiling_panel = file_config.debug.profiling_panel,
      .debug_physics_panel = file_config.debug.physics.panel,
      .debug_physics_server_bodies = file_config.debug.physics.server_bodies,
      .debug_physics_hitboxes = file_config.debug.physics.hitboxes,
#endif
  };
  auto parts = std::make_unique<ClientProcess::Parts>();
  parts->pack = std::move(loaded->pack);
  parts->runtime = std::make_unique<ClientRuntime>(config, std::move(loaded->content));
  return std::make_unique<ClientProcess>(std::move(parts));
}

}  // namespace

application::Outcome ListReplays(const config::ClientConfig& file_config) {
  // Not run by application::Execute, so it raises the resolution itself.
  const application::TimerResolution timer_resolution;
  auto listed = failure::Guard(failure::Code::kWorkerFailed, [&file_config] { return QueryReplays(file_config); });
  if (!listed) {
    listed.error().context.push_back({.key = "phase", .value = "list_replays"});
    return std::move(listed.error());
  }
  return *std::move(listed);
}

application::Lifecycle<ClientProcess> ClientLifecycle(const config::ClientConfig& file_config,
                                                      std::optional<harness::Script> reenactment,
                                                      std::optional<std::string> replay, boost::uuids::uuid run) {
  config::ClientConfig played = file_config;
  // A Captured player plays its capture's Character, not the config's.
  if (reenactment.has_value()) {
    played.character = reenactment->character;
  }
  return {
      // Once, process-wide, before any ClientRuntime is constructed (link.h).
      .initialize = [] { return link::Init(); },
      .construct = [played = std::move(played), reenactment = std::move(reenactment), replay = std::move(replay),
                    run] { return ConstructProcess(played, reenactment, replay, run); },
      .run = [](ClientProcess& process) { return process.Run(); },
  };
}

}  // namespace augusta::client
