#include <expected>
#include <filesystem>
#include <format>
#include <print>
#include <string>
#include <utility>

#include "augusta/assets.h"
#include "augusta/config.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/version.h"
#include "runtime.h"
#include "scene_loader.h"

namespace {

// Reads the Ed25519 public key and loads the pack against it; the error is
// the message to print, so main() only decides to exit.
std::expected<augusta::assets::Pack, std::string> LoadVerifiedPack(const std::filesystem::path& pack_path,
                                                                   const std::filesystem::path& public_key_path) {
  const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(public_key_path);
  if (!public_key) {
    return std::unexpected(std::format("could not read Ed25519 public key from {}", public_key_path.string()));
  }
  auto pack = augusta::assets::Pack::Load(pack_path, *public_key);
  if (!pack) {
    return std::unexpected(
        std::format("client pack {} {}", pack_path.string(), augusta::assets::DescribeLoadError(pack.error())));
  }
  return std::move(*pack);
}

}  // namespace

int main(int argc, char** argv) {
  augusta::logging::Init();

  // Settings come from a config file - augustac.yaml next to the executable
  // unless --config names another (ADR-0034) - not from the command line.
  const auto config_file =
      augusta::config::ResolveConfigFile(argc, argv, "augustac", augusta::config::kClientConfigFileName);
  if (!config_file) {
    std::println(stderr, "{}", config_file.error());
    return 1;
  }
  const auto file_config = augusta::config::LoadClientConfig(*config_file);
  if (!file_config) {
    std::println(stderr, "{}", file_config.error());
    return 1;
  }
  LI("subsystem=client event=starting version={}", augusta::EngineVersion());

  // Verified before anything else starts (no renderer/audio device,
  // network socket, or thread is spun up yet) - a bad pack or key means
  // this process exits here, never partially running against untrusted
  // content (ADR-0018, ARCHITECTURE.md §8).
  const std::filesystem::path& pack_path = file_config->pack_path;
  const auto pack = LoadVerifiedPack(pack_path, file_config->public_key_path);
  if (!pack) {
    std::println(stderr, "{}", pack.error());
    return 1;
  }
  LI("subsystem=client event=pack_verified path={}", pack_path.string());

  // Only the scene graph and its meshes are consumed so far (what the
  // renderer draws); collision/hitbox/texture/audio resolution waits for
  // the ECS component shapes and gameplay code that will use them. Built
  // here, before the window opens, so a pack without a usable scene exits
  // like a bad pack does.
  const auto scene = augusta::client::LoadRenderScene(*pack);
  if (!scene) {
    std::println(stderr, "client pack {}: {}", pack_path.string(), scene.error());
    return 1;
  }
  LI("subsystem=client event=scene_loaded meshes={}", scene->meshes.size());

  // augusta::networking::Init() must run once, process-wide, before any
  // Client/Server is constructed - see networking.h.
  augusta::networking::Init();

  augusta::runtime::Config config;
  config.renderer.title = "augusta";
  // Direct IP:port only, no server discovery (ARCHITECTURE.md §3).
  config.server.address = file_config->server_address;

  augusta::runtime::ClientRuntime runtime(config, *scene);
  runtime.Run();

  return 0;
}
