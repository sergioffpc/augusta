#include "augusta/logging.h"
#include "augusta/version.h"

int main() {
  augusta::logging::Init();
  INFO("augusta server v{} starting", augusta::EngineVersion());
  return 0;
}
