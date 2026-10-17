#include "augusta/server_application.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "augusta/application.h"
#include "augusta/assets.h"
#include "augusta/capture_retention.h"
#include "augusta/capturer.h"
#include "augusta/config.h"
#include "augusta/content.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/host.h"
#include "augusta/link.h"
#include "augusta/logging.h"
#include "augusta/policy_loader.h"
#include "augusta/replay_server.h"
#include "augusta/server.h"
#include "augusta/server_config.h"

namespace augusta::server {

namespace {

constexpr std::uintmax_t kMiB = std::uintmax_t{1024} * 1024;

failure::Failure ContentFailure(const std::filesystem::path& pack_path, std::string detail) {
  return {.code = failure::Code::kInvalidContent,
          .context = {{.key = "path", .value = pack_path.string()}},
          .detail = std::move(detail)};
}

// A replay server's runtime (ADR-0051), on the content loaded from pack: each
// Replay's World makes the pack's Game policy afresh, so the policy is kept
// as what makes it rather than as the one engine content loaded.
std::expected<std::unique_ptr<ServerRuntime>, failure::Failure> ConstructReplayRuntime(
    const config::ServerConfig& file_config, const assets::Pack& pack, scenario::Content content,
    failure::Faults* faults) {
  auto policy = scenario::LoadPolicyMaker(pack);
  if (!policy) {
    return std::unexpected(ContentFailure(pack.Path(), scenario::DescribePolicyLoadError(policy.error())));
  }
  const host::ReplayServerConfig replay_config{
      .tick_rate_hz = file_config.tick_rate_hz,
      .parameters = content.parameters,
      .listen = {.address = file_config.listen_address},
      .server_pack = pack.Hash(),
      .captures = file_config.replay_captures,
      .max_viewers = file_config.replay_max_viewers,
      .faults = faults,
  };
  return std::make_unique<ServerRuntime>(replay_config, file_config.metrics_port, std::move(content.scenario),
                                         *std::move(policy), faults);
}

// Verifies the pack the file's settings name, loads its content and constructs
// the runtime from both. Verified before anything else starts (no socket,
// world, or thread is spun up yet) - a bad pack or key means this process exits
// here, never partially running against untrusted content (ADR-0018,
// ARCHITECTURE.md §8). Nothing in the content refers back to the pack, so it
// goes once the content is loaded.
std::expected<std::unique_ptr<ServerRuntime>, failure::Failure> ConstructRuntime(
    const config::ServerConfig& file_config, failure::Faults* faults) {
  const std::filesystem::path& pack_path = file_config.pack_path;
  const auto pack = assets::LoadVerifiedPack(pack_path, file_config.public_key_path);
  if (!pack) {
    return std::unexpected(ContentFailure(
        pack_path, assets::DescribeVerifiedPackError(pack.error(), pack_path, file_config.public_key_path)));
  }
  LOG_INFO("subsystem=server event=pack_verified path={}", pack_path.string());

  auto content = scenario::LoadServerContent(*pack, file_config.tick_rate_hz);
  if (!content) {
    return std::unexpected(std::move(content.error()));
  }
  if (!file_config.replay_captures.empty()) {
    return ConstructReplayRuntime(file_config, *pack, *std::move(content), faults);
  }

  const host::HostConfig host_config{
      .tick_rate_hz = file_config.tick_rate_hz,
      // Every client is sent the rate and these when it joins and predicts with
      // them, so the config file and the scenario's script are the only places
      // they are set.
      .parameters = content->parameters,
      .listen = {.address = file_config.listen_address},
      .server_pack = pack->Hash(),
      .capture_directory = file_config.capture_directory,
      .capture_mode = file_config.strict_capture ? host::CaptureMode::kStrict : host::CaptureMode::kOptional,
      .capture_retention = host::CaptureRetention{.max_files = file_config.capture_max_files,
                                                  .max_bytes = file_config.capture_max_mib.transform(
                                                      [](std::uint32_t mib) { return std::uintmax_t{mib} * kMiB; })},
      .reenactments = file_config.reenactments,
      .faults = faults,
  };
  return std::make_unique<ServerRuntime>(host_config, file_config.metrics_port, std::move(content->scenario),
                                         std::move(content->policy), faults);
}

}  // namespace

std::expected<config::ServerConfig, failure::Failure> ReadServerConfig(
    const std::expected<config::CommandLine, config::ConfigError>& command_line) {
  return command_line
      .and_then([](const config::CommandLine& read) { return config::LoadServerConfig(read.config_file); })
      .transform_error([](const config::ConfigError& error) {
        return failure::Failure{.code = failure::Code::kInvalidConfiguration,
                                .context = {},
                                .detail = config::DescribeServerConfigError(error)};
      });
}

application::Lifecycle<ServerRuntime> ServerLifecycle(const config::ServerConfig& file_config,
                                                      failure::Faults* faults) {
  return {
      // link::Init() must run once, process-wide, before any Server is
      // constructed - see link.h.
      .initialize = [faults] { return link::Init(faults); },
      .construct = [file_config, faults] { return ConstructRuntime(file_config, faults); },
      .run = [](ServerRuntime& runtime) { return runtime.Run(); },
  };
}

}  // namespace augusta::server
