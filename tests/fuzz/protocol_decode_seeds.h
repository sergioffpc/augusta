#ifndef AUGUSTA_TESTS_FUZZ_PROTOCOL_DECODE_SEEDS_H_
#define AUGUSTA_TESTS_FUZZ_PROTOCOL_DECODE_SEEDS_H_

#include <string>
#include <vector>

#include "augusta/protocol.h"

// The protocol_decode seeds that protocol_decode_seeds_test.cpp checks: the
// messages both kinds of build have are its own, and those of a Development
// build alone (ADR-0074) come from a file the build's kind picks
// (tests/CMakeLists.txt), so no file leaves a seed out by `#if`.
namespace augusta::protocol_decode_seeds {

// One seed: its file's name in the corpus, and the message Encode writes it from.
struct Seed {
  std::string name;
  protocol::MessageWire message;
};

// The seeds of the messages only a Development build has: none in a Shipping build.
std::vector<Seed> DevelopmentSeeds();

}  // namespace augusta::protocol_decode_seeds

#endif  // AUGUSTA_TESTS_FUZZ_PROTOCOL_DECODE_SEEDS_H_
