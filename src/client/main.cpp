#include "augusta/logging.h"
#include "augusta/version.h"

int main() {
  augusta::logging::Init();
  INFO("augusta client v{} starting", augusta::EngineVersion());
  return 0;
}
