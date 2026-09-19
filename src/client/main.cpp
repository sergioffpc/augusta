#include <filesystem>
#include <print>

#include "augusta/assets.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/version.h"
#include "runtime.h"
#include "scene_loader.h"

int main(int argc, char** argv) {
  augusta::logging::Init();
  LI("subsystem=client event=starting version={}", augusta::EngineVersion());

  // Verified before anything else starts (no renderer/audio device,
  // network socket, or thread is spun up yet) - a bad pack or key means
  // this process exits here, never partially running against untrusted
  // content (ADR-0018, ARCHITECTURE.md §8).
  if (argc != 3) {
    std::println(stderr, "usage: augustac <client_pack_path> <public_key_path>");
    return 1;
  }
  const std::filesystem::path pack_path = argv[1];
  const std::filesystem::path public_key_path = argv[2];

  const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(public_key_path);
  if (!public_key) {
    std::println(stderr, "could not read Ed25519 public key from {}", public_key_path.string());
    return 1;
  }
  const auto pack = augusta::assets::Pack::Load(pack_path, *public_key);
  if (!pack) {
    std::println(stderr, "client pack {} {}", pack_path.string(), augusta::assets::DescribeLoadError(pack.error()));
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
  // TODO(sergioffpc): hardcoded placeholder - there's no command-line/
  // config parsing yet, and no server discovery beyond direct IP:port
  // (ARCHITECTURE.md §3).
  config.server.address = "127.0.0.1:27015";

  augusta::runtime::ClientRuntime runtime(config, *scene);
  runtime.Run();

  return 0;
}
