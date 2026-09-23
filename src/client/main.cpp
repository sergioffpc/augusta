#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <utility>

#include "augusta/assets.h"
#include "augusta/config.h"
#include "augusta/harness.h"
#include "augusta/logging.h"
#include "augusta/map.h"
#include "augusta/networking.h"
#include "augusta/version.h"
#include "runtime.h"
#include "scene_loader.h"

namespace {

enum class PackFailure {
  kPublicKeyUnreadable,
  kPackRejected,
};

struct PackError {
  PackFailure failure;
  // Why Pack::Load refused the pack; only meaningful for kPackRejected.
  augusta::assets::LoadError load_error{};
};

// Reads the Ed25519 public key and loads the pack against it.
std::expected<augusta::assets::Pack, PackError> LoadVerifiedPack(const std::filesystem::path& pack_path,
                                                                 const std::filesystem::path& public_key_path) {
  const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(public_key_path);
  if (!public_key) {
    return std::unexpected(PackError{.failure = PackFailure::kPublicKeyUnreadable});
  }
  auto pack = augusta::assets::Pack::Load(pack_path, *public_key);
  if (!pack) {
    return std::unexpected(PackError{.failure = PackFailure::kPackRejected, .load_error = pack.error()});
  }
  return std::move(*pack);
}

std::string DescribePackError(const PackError& error, const std::filesystem::path& pack_path,
                              const std::filesystem::path& public_key_path) {
  switch (error.failure) {
    case PackFailure::kPublicKeyUnreadable:
      return std::format("could not read Ed25519 public key from {}", public_key_path.string());
    case PackFailure::kPackRejected:
      return std::format("client pack {} {}", pack_path.string(), augusta::assets::DescribeLoadError(error.load_error));
  }
  return "unknown pack error";
}

// Settings come from a config file - augustac.yaml next to the executable
// unless --config names another (ADR-0034) - not from the command line.
std::expected<augusta::config::ClientConfig, augusta::config::ConfigError> LoadConfig(int argc, char** argv) {
  const auto config_file =
      augusta::config::ResolveConfigFile(argc, argv, "augustac", augusta::config::kClientConfigFileName);
  if (!config_file) {
    return std::unexpected(config_file.error());
  }
  return augusta::config::LoadClientConfig(*config_file);
}

// Only the scene graph and its meshes are consumed so far (what the renderer
// draws); collision/hitbox/texture/audio resolution waits for the ECS
// component shapes and gameplay code that will use them. Reports what is
// wrong and returns nullopt.
std::optional<augusta::renderer::Scene> LoadRenderScene(const augusta::assets::Pack& pack,
                                                        const std::filesystem::path& pack_path) {
  auto scene = augusta::client::LoadRenderScene(pack);
  if (!scene) {
    std::println(stderr, "client pack {}: {}", pack_path.string(), augusta::client::DescribeSceneError(scene.error()));
    return std::nullopt;
  }
  LI("subsystem=client event=scene_loaded meshes={}", scene->meshes.size());
  return *std::move(scene);
}

// The one character every RemotePlayer is drawn as (issue #82 - no per-player
// character selection yet, ADR-0040/ADR-0041). Reports what is wrong and
// returns nullopt.
std::optional<augusta::renderer::SceneMesh> LoadRemotePlayerMesh(const augusta::assets::Pack& pack,
                                                                 const std::filesystem::path& pack_path) {
  auto mesh = augusta::client::LoadRemotePlayerMesh(pack);
  if (!mesh) {
    std::println(stderr, "client pack {}: {}", pack_path.string(), augusta::client::DescribeSceneError(mesh.error()));
    return std::nullopt;
  }
  return *std::move(mesh);
}

// The same collision the server builds from its own pack, so the client's
// prediction and the server's simulation agree on where the walls are. Reports
// what is wrong and returns nullopt.
std::optional<augusta::runtime::Map> LoadMap(const augusta::assets::Pack& pack,
                                             const std::filesystem::path& pack_path) {
  auto collision = augusta::map::LoadCollision(pack);
  if (!collision) {
    std::println(stderr, "client pack {}: {}", pack_path.string(), augusta::map::DescribeMapError(collision.error()));
    return std::nullopt;
  }
  LI("subsystem=client event=map_loaded colliders={}", collision->size());
  return augusta::runtime::Map{.collision = *std::move(collision)};
}

}  // namespace

int main(int argc, char** argv) {
  augusta::logging::Init();

  const auto file_config = LoadConfig(argc, argv);
  if (!file_config) {
    std::println(stderr, "{}", augusta::config::DescribeConfigError(file_config.error()));
    return 1;
  }
  // ParseClientConfig already validated log_level, so this is never nullopt.
  augusta::logging::SetLogLevel(*augusta::logging::ParseSeverity(file_config->log_level));
  LI("subsystem=client event=starting version={}", augusta::EngineVersion());

  // Verified before anything else starts (no renderer/audio device,
  // network socket, or thread is spun up yet) - a bad pack or key means
  // this process exits here, never partially running against untrusted
  // content (ADR-0018, ARCHITECTURE.md §8).
  const std::filesystem::path& pack_path = file_config->pack_path;
  const auto pack = LoadVerifiedPack(pack_path, file_config->public_key_path);
  if (!pack) {
    std::println(stderr, "{}", DescribePackError(pack.error(), pack_path, file_config->public_key_path));
    return 1;
  }
  LI("subsystem=client event=pack_verified path={}", pack_path.string());

  auto scene = LoadRenderScene(*pack, pack_path);
  if (!scene) {
    return 1;
  }

  auto remote_player_mesh = LoadRemotePlayerMesh(*pack, pack_path);
  if (!remote_player_mesh) {
    return 1;
  }

  auto map = LoadMap(*pack, pack_path);
  if (!map) {
    return 1;
  }

  // augusta::networking::Init() must run once, process-wide, before any
  // Client/Server is constructed - see networking.h.
  augusta::networking::Init();

  augusta::runtime::Config config;
  config.renderer.title = "augusta";
  // Direct IP:port only, no server discovery (ARCHITECTURE.md §3).
  config.server.address = file_config->server_address;
  config.input = file_config->input;
  config.character = file_config->character;

  augusta::runtime::ClientRuntime runtime(config, *std::move(map), *scene, *remote_player_mesh);
  if (const auto failure = runtime.Run(); failure.has_value()) {
    // No reconnecting and no connection screen: say what happened and exit.
    std::println(stderr, "{}", augusta::harness::DescribeFailure(*failure));
    return 1;
  }

  return 0;
}
