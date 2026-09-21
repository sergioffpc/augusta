#include <csignal>
#include <expected>
#include <filesystem>
#include <optional>
#include <print>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/config.h"
#include "augusta/logging.h"
#include "augusta/map.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/version.h"
#include "runtime.h"

namespace {

// Signal handlers can't capture context, so this is the only way to
// reach the one ServerRuntime main() constructs - set just before
// Run() is called, never reassigned afterward.
augusta::runtime::ServerRuntime* g_runtime = nullptr;

extern "C" void HandleShutdownSignal(int /*signal*/) {
  if (g_runtime != nullptr) {
    g_runtime->Stop();
  }
}

// Settings come from a config file - augustad.yaml next to the executable
// unless --config names another (ADR-0034) - not from the command line.
std::expected<augusta::config::ServerConfig, augusta::config::ConfigError> LoadConfig(int argc, char** argv) {
  const auto config_file =
      augusta::config::ResolveConfigFile(argc, argv, "augustad", augusta::config::kServerConfigFileName);
  if (!config_file) {
    return std::unexpected(config_file.error());
  }
  return augusta::config::LoadServerConfig(*config_file);
}

// What the pack's map gives the server: its collision and where players spawn.
// Hitboxes wait for the gameplay code that will use them.
struct Map {
  std::vector<augusta::physics::CollisionMesh> collision;
  std::vector<augusta::math::Vec3> spawn_points;
};

// Built before any socket or thread starts, so a pack without a usable map
// exits like a bad pack does. Reports what is wrong and returns nullopt.
std::optional<Map> LoadMap(const augusta::assets::Pack& pack, const std::filesystem::path& pack_path) {
  auto collision = augusta::map::LoadCollision(pack);
  if (!collision) {
    std::println(stderr, "server pack {}: {}", pack_path.string(), augusta::map::DescribeMapError(collision.error()));
    return std::nullopt;
  }
  auto spawn_points = augusta::map::LoadSpawnPoints(pack);
  if (!spawn_points) {
    std::println(stderr, "server pack {}: {}", pack_path.string(),
                 augusta::map::DescribeMapError(spawn_points.error()));
    return std::nullopt;
  }
  LI("subsystem=server event=map_loaded colliders={} spawn_points={}", collision->size(), spawn_points->size());
  return Map{.collision = *std::move(collision), .spawn_points = *std::move(spawn_points)};
}

// What ServerRuntime is built from: the file's settings and what the pack supplied.
augusta::runtime::Config BuildRuntimeConfig(const augusta::config::ServerConfig& file_config, Map map) {
  augusta::runtime::Config config;
  // TODO(sergioffpc): hardcoded placeholder - script_path assumes an asset
  // pack layout the asset pipeline (ROADMAP.md M2) hasn't built yet.
  config.script_path = "scripts/round.lua";
  config.listen.address = file_config.listen_address;
  config.collision = std::move(map.collision);
  config.spawn_points = std::move(map.spawn_points);
  // Every client is sent these when it joins and predicts with them, so
  // tuning lives here alone.
  config.parameters.stamina = {
      .deplete_per_second = file_config.stamina_deplete_per_second,
      .regen_per_second = file_config.stamina_regen_per_second,
      .forced_walk_below = file_config.stamina_forced_walk_below,
  };
  return config;
}

}  // namespace

int main(int argc, char** argv) {
  augusta::logging::Init();

  const auto file_config = LoadConfig(argc, argv);
  if (!file_config) {
    std::println(stderr, "{}", augusta::config::DescribeConfigError(file_config.error()));
    return 1;
  }
  LI("subsystem=server event=starting version={}", augusta::EngineVersion());

  // Verified before anything else starts (no socket, world, or thread is
  // spun up yet) - a bad pack or key means this process exits here, never
  // partially running against untrusted content (ADR-0018,
  // ARCHITECTURE.md §8).
  const std::filesystem::path& pack_path = file_config->pack_path;
  const std::filesystem::path& public_key_path = file_config->public_key_path;

  const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(public_key_path);
  if (!public_key) {
    std::println(stderr, "could not read Ed25519 public key from {}", public_key_path.string());
    return 1;
  }
  const auto pack = augusta::assets::Pack::Load(pack_path, *public_key);
  if (!pack) {
    std::println(stderr, "server pack {} {}", pack_path.string(), augusta::assets::DescribeLoadError(pack.error()));
    return 1;
  }
  LI("subsystem=server event=pack_verified path={}", pack_path.string());

  auto map = LoadMap(*pack, pack_path);
  if (!map) {
    return 1;
  }

  // augusta::networking::Init() must run once, process-wide, before any
  // Client/Server is constructed - see networking.h.
  augusta::networking::Init();

  const augusta::runtime::Config config = BuildRuntimeConfig(*file_config, *std::move(map));
  augusta::runtime::ServerRuntime runtime(config);
  g_runtime = &runtime;
  std::signal(SIGINT, HandleShutdownSignal);
  std::signal(SIGTERM, HandleShutdownSignal);

  runtime.Run();

  return 0;
}
