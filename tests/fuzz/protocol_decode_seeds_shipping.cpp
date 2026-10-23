#include <vector>

#include "protocol_decode_seeds.h"

// A Shipping build has none of a Development build's messages (ADR-0074), so
// none of their seeds: its decoder replays their committed files all the same,
// each refused as a type it lacks.
namespace augusta::protocol_decode_seeds {

std::vector<Seed> DevelopmentSeeds() { return {}; }

}  // namespace augusta::protocol_decode_seeds
