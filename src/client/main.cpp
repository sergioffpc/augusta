#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/version.h"
#include "runtime.h"

int main() {
  augusta::logging::Init();
  INFO("subsystem=client event=starting version={}", augusta::EngineVersion());

  // augusta::networking::Init() must run once, process-wide, before any
  // Client/Server is constructed - see networking.h.
  augusta::networking::Init();

  augusta::runtime::Config config;
  config.renderer.title = "augusta";
  // TODO(sergioffpc): hardcoded placeholder - there's no command-line/
  // config parsing yet, and no server discovery beyond direct IP:port
  // (ARCHITECTURE.md §3).
  config.server.address = "127.0.0.1:27015";

  augusta::runtime::ClientRuntime runtime(config);
  runtime.Run();

  return 0;
}
