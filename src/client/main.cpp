#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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
    LE("subsystem=client event=scene_loading_failed path={} error={}", pack_path.string(),
       augusta::client::DescribeSceneError(scene.error()));
    return std::nullopt;
  }
  LI("subsystem=client event=scene_loaded meshes={}", scene->meshes.size());
  return *std::move(scene);
}

// Loads a character's mesh from pack by its index into the scenario's
// characters (ADR-0042), which pack must outlive. Reports what is wrong with the
// character list and returns nullopt.
std::optional<augusta::runtime::CharacterMeshLoader> CharacterMeshLoaderFor(const augusta::assets::Pack& pack,
                                                                            const std::filesystem::path& pack_path) {
  auto characters = pack.ResolveCharacters();
  if (!characters) {
    LE("subsystem=client event=character_mesh_loading_failed path={} error={}", pack_path.string(),
       std::format("{} {}", augusta::assets::kCharactersPath,
                   augusta::assets::DescribeResolveError(characters.error(), "character list")));
    return std::nullopt;
  }
  return [&pack, characters = *std::move(characters)](std::uint8_t character) {
    return augusta::client::LoadCharacterMesh(characters, character,
                                              [&pack](std::string_view path) { return pack.ResolveMesh(path); });
  };
}

// What to tell whoever runs the process about why the client stopped.
std::string DescribeRunFailure(const augusta::runtime::Failure& failure, const std::filesystem::path& pack_path) {
  if (const auto* session = std::get_if<augusta::harness::Failure>(&failure)) {
    return augusta::harness::DescribeFailure(*session);
  }
  return std::format("client pack {}: {}", pack_path.string(),
                     augusta::client::DescribeSceneError(std::get<augusta::client::SceneError>(failure)));
}

// The same collision the server builds from its own pack, so the client's
// prediction and the server's simulation agree on where the walls are. Reports
// what is wrong and returns nullopt.
std::optional<augusta::runtime::Map> LoadMap(const augusta::assets::Pack& pack,
                                             const std::filesystem::path& pack_path) {
  auto collision = augusta::map::LoadCollision(pack);
  if (!collision) {
    LE("subsystem=client event=map_loading_failed path={} error={}", pack_path.string(),
       augusta::map::DescribeMapError(collision.error()));
    return std::nullopt;
  }
  LI("subsystem=client event=map_loaded colliders={}", collision->size());
  return augusta::runtime::Map{.collision = *std::move(collision)};
}

struct Content {
  augusta::renderer::Scene scene;
  augusta::runtime::Map map;
  augusta::runtime::CharacterMeshLoader load_character_mesh;
};

enum class ContentError {
  kSceneLoading,
  kCharacterMeshLoading,
  kMapLoading,
};

std::string_view DescribeContentError(ContentError error) {
  switch (error) {
    case ContentError::kSceneLoading:
      return "scene loading failed";
    case ContentError::kCharacterMeshLoading:
      return "character mesh loading failed";
    case ContentError::kMapLoading:
      return "map loading failed";
  }
  return "unknown content error";
}

std::expected<Content, ContentError> LoadClientContent(const augusta::assets::Pack& pack,
                                                       const std::filesystem::path& pack_path) {
  auto scene = LoadRenderScene(pack, pack_path);
  if (!scene) {
    return std::unexpected(ContentError::kSceneLoading);
  }

  // A character's mesh is loaded only once another player in the Lobby brings it (ADR-0043).
  auto load_character_mesh = CharacterMeshLoaderFor(pack, pack_path);
  if (!load_character_mesh) {
    return std::unexpected(ContentError::kCharacterMeshLoading);
  }

  auto map = LoadMap(pack, pack_path);
  if (!map) {
    return std::unexpected(ContentError::kMapLoading);
  }

  return Content{
      .scene = *std::move(scene), .map = *std::move(map), .load_character_mesh = *std::move(load_character_mesh)};
}

augusta::runtime::Config BuildRuntimeConfig(const augusta::config::ClientConfig& file_config,
                                            const augusta::assets::Pack& pack) {
  augusta::runtime::Config config;
  config.renderer.title = "augusta";
  // Direct IP:port only, no server discovery (ARCHITECTURE.md §3).
  config.server.address = file_config.server_address;
  config.input = file_config.input;
  config.character = file_config.character;
  config.client_pack = pack.Hash();
  return config;
}

int Run(const augusta::config::ClientConfig& file_config, const augusta::assets::Pack& pack,
        const std::filesystem::path& pack_path, Content content) {
  // augusta::networking::Init() must run once, process-wide, before any
  // Client/Server is constructed - see networking.h.
  augusta::networking::Init();

  const augusta::runtime::Config config = BuildRuntimeConfig(file_config, pack);
  augusta::runtime::ClientRuntime runtime(config, std::move(content.map), content.scene,
                                          std::move(content.load_character_mesh));
  if (const auto failure = runtime.Run(); failure.has_value()) {
    // No reconnecting and no connection screen: say what happened and exit.
    LE("subsystem=client event=run_failed path={} error={}", pack_path.string(),
       DescribeRunFailure(*failure, pack_path));
    return 1;
  }

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  augusta::logging::Init();

  const auto file_config = LoadConfig(argc, argv);
  if (!file_config) {
    LE("subsystem=client event=config_loading_failed error={}",
       augusta::config::DescribeConfigError(file_config.error()));
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
    LE("subsystem=client event=pack_verification_failed path={} error={}", pack_path.string(),
       DescribePackError(pack.error(), pack_path, file_config->public_key_path));
    return 1;
  }
  LI("subsystem=client event=pack_verified path={}", pack_path.string());

  auto content = LoadClientContent(*pack, pack_path);
  if (!content) {
    LE("subsystem=client event=content_loading_failed path={} error={}", pack_path.string(),
       DescribeContentError(content.error()));
    return 1;
  }

  return Run(*file_config, *pack, pack_path, *std::move(content));
}
