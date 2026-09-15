#include <csignal>

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

int main() {
  augusta::logging::Init();
  INFO("augusta server v{} starting", augusta::EngineVersion());

  // augusta::networking::Init() must run once, process-wide, before any
  // Client/Server is constructed - see networking.h.
  augusta::networking::Init();

  augusta::runtime::Config config;
  // TODO(sergioffpc): hardcoded placeholders - there's no command-line/
  // config parsing yet, and script_path assumes an asset pack layout
  // the asset pipeline (ROADMAP.md M5) hasn't built yet.
  config.script_path = "scripts/round.lua";
  config.listen.address = "0.0.0.0:27015";

  augusta::runtime::ServerRuntime runtime(config);
  g_runtime = &runtime;
  std::signal(SIGINT, HandleShutdownSignal);
  std::signal(SIGTERM, HandleShutdownSignal);

  runtime.Run();

  return 0;
}
