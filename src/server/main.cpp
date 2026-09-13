#include <spdlog/spdlog.h>

#include "augusta/engine_version.h"

int main() {
  spdlog::info("augusta server v{} starting", augusta::EngineVersion());
  return 0;
}
