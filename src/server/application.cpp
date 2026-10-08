#include "application.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "augusta/application.h"
#include "augusta/assets.h"
#include "augusta/config.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/server_config.h"
#include "content.h"
#include "host.h"
#include "runtime.h"

namespace augusta::server {

namespace {

failure::Failure ContentFailure(const std::filesystem::path& pack_path, std::string detail) {
  return {.code = failure::Code::kInvalidContent,
          .context = {{.key = "path", .value = pack_path.string()}},
          .detail = std::move(detail)};
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
  LI("subsystem=server event=pack_verified path={}", pack_path.string());

  auto content = LoadServerContent(*pack, file_config.tick_rate_hz);
  if (!content) {
    return std::unexpected(ContentFailure(pack_path, std::string(DescribeContentError(content.error()))));
  }

  const HostConfig host_config{
      .tick_rate_hz = file_config.tick_rate_hz,
      // Every client is sent the rate and these when it joins and predicts with
      // them, so the config file and the scenario's script are the only places
      // they are set.
      .parameters = content->parameters,
      .listen = {.address = file_config.listen_address},
      .recording = file_config.recording_path,
      .server_pack = pack->Hash(),
  };
  if (!host_config.recording.empty()) {
    LI("subsystem=server event=recording path={}", host_config.recording.string());
  }
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
      // networking::Init() must run once, process-wide, before any Server is
      // constructed - see networking.h.
      .initialize = [faults]() -> std::expected<void, failure::Failure> {
        return failure::Guard(failure::Code::kTransportInitFailed, [faults] {
          if (faults != nullptr) {
            faults->ThrowIfTripped(failure::Site::kDependencyInit);
          }
          networking::Init();
        });
      },
      .construct = [file_config, faults] { return ConstructRuntime(file_config, faults); },
      .run = [](ServerRuntime& runtime) { return runtime.Run(); },
  };
}

}  // namespace augusta::server
