#include <spdlog/spdlog.h>

#include "augusta/version.h"

int main() {
  spdlog::info("augusta client v{} starting", augusta::EngineVersion());
  return 0;
}
