#include "application.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "augusta/assets.h"
#include "augusta/client_config.h"
#include "augusta/config.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/harness.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "character_loader.h"
#include "content.h"

namespace augusta::client {

namespace {

failure::Failure ContentFailure(const std::filesystem::path& pack_path, std::string detail) {
  return {.code = failure::Code::kInvalidContent,
          .context = {{.key = "path", .value = pack_path.string()}},
          .detail = std::move(detail)};
}

failure::Code SessionCode(harness::FailureKind kind) {
  switch (kind) {
    case harness::FailureKind::kRefused:
      return failure::Code::kJoinRefused;
    case harness::FailureKind::kServerUnreachable:
      return failure::Code::kServerUnreachable;
    case harness::FailureKind::kConnectionLost:
      return failure::Code::kPeerConnectionLost;
  }
  // A value no enumerator names: the Session still ended.
  return failure::Code::kPeerConnectionLost;
}

}  // namespace

std::expected<config::ClientConfig, failure::Failure> ReadClientConfig(
    const std::expected<config::CommandLine, config::ConfigError>& command_line) {
  return command_line
      .and_then([](const config::CommandLine& read) { return config::LoadClientConfig(read.config_file); })
      .transform_error([](const config::ConfigError& error) {
        return failure::Failure{
            .code = failure::Code::kInvalidConfiguration, .context = {}, .detail = config::DescribeConfigError(error)};
      });
}

std::expected<void, failure::Failure> InitializeClientTransport(failure::Faults* faults) {
  return failure::Guard(failure::Code::kTransportInitFailed, [faults] {
    if (faults != nullptr) {
      faults->ThrowIfTripped(failure::Site::kDependencyInit);
    }
    networking::Init();
  });
}

std::expected<LoadedClient, failure::Failure> LoadClient(const config::ClientConfig& file_config) {
  const std::filesystem::path& pack_path = file_config.pack_path;
  auto verified = assets::LoadVerifiedPack(pack_path, file_config.public_key_path);
  if (!verified) {
    return std::unexpected(ContentFailure(
        pack_path, assets::DescribeVerifiedPackError(verified.error(), pack_path, file_config.public_key_path)));
  }
  auto pack = std::make_unique<const assets::Pack>(*std::move(verified));
  LI("subsystem=client event=pack_verified path={}", pack->Path().string());

  auto content = LoadClientContent(*pack, file_config.character);
  if (!content) {
    return std::unexpected(ContentFailure(pack_path, std::string(DescribeContentError(content.error()))));
  }
  return LoadedClient{.pack = std::move(pack), .content = *std::move(content)};
}

failure::Failure ClassifySessionFailure(const harness::Failure& session) {
  return {.code = SessionCode(session.kind), .context = {}, .detail = harness::DescribeFailure(session)};
}

failure::Failure ClassifyCharacterError(const CharacterError& error) {
  return {.code = failure::Code::kInvalidContent,
          .context = {{.key = "character", .value = error.character}},
          .detail = DescribeCharacterError(error)};
}

}  // namespace augusta::client
