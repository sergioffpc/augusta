#include <vector>

#include "augusta/protocol.h"
#include "protocol_decode_seeds.h"

// A Development build's messages' seeds (ADR-0038, ADR-0074).
namespace augusta::protocol_decode_seeds {

std::vector<Seed> DevelopmentSeeds() {
  return {
      {.name = "debug_subscribe",
       .message = protocol::DebugSubscribeWire{.flags = protocol::DebugSubscribeWire::kShotJudgements}},
  };
}

}  // namespace augusta::protocol_decode_seeds
