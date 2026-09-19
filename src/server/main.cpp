#include <csignal>
#include <filesystem>
#include <print>

#include "augusta/assets.h"
#include "augusta/logging.h"
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

}  // namespace

int main(int argc, char** argv) {
  augusta::logging::Init();
  LI("subsystem=server event=starting version={}", augusta::EngineVersion());

  // Verified before anything else starts (no socket, world, or thread is
  // spun up yet) - a bad pack or key means this process exits here, never
  // partially running against untrusted content (ADR-0018,
  // ARCHITECTURE.md §8).
  if (argc != 3) {
    std::println(stderr, "usage: augustad <server_pack_path> <public_key_path>");
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
    std::println(stderr, "server pack {} {}", pack_path.string(), augusta::assets::DescribeLoadError(pack.error()));
    return 1;
  }
  LI("subsystem=server event=pack_verified path={}", pack_path.string());
  // pack itself is dropped here - resolving specific assets from it
  // (ResolveCollision/ResolveSpawnPoint/...) is out of scope for this
  // startup gate (issue #60); that's future work once there's ECS
  // component shape/gameplay code ready to consume what it resolves.

  // augusta::networking::Init() must run once, process-wide, before any
  // Client/Server is constructed - see networking.h.
  augusta::networking::Init();

  augusta::runtime::Config config;
  // TODO(sergioffpc): hardcoded placeholders - there's no command-line/
  // config parsing yet, and script_path assumes an asset pack layout
  // the asset pipeline (ROADMAP.md M2) hasn't built yet.
  config.script_path = "scripts/round.lua";
  config.listen.address = "0.0.0.0:27015";

  augusta::runtime::ServerRuntime runtime(config);
  g_runtime = &runtime;
  std::signal(SIGINT, HandleShutdownSignal);
  std::signal(SIGTERM, HandleShutdownSignal);

  runtime.Run();

  return 0;
}
