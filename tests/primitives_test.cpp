#include "augusta/primitives.h"

#include <concepts>
#include <cstdint>

#include <gtest/gtest.h>

// Links augusta_primitives alone, so this file compiles only while the header
// includes nothing of the engine's: the module's whole point.
namespace {

using augusta::primitives::kMaxCommandsPerMessage;
using augusta::primitives::kMaxPlayers;
using augusta::primitives::kMaxRecoilKicks;
using augusta::primitives::Sequence;
using augusta::primitives::Tick;

// ADR-0038: a tick and a command sequence are 64 bits wide and never wrap,
// so every receiver orders them as plain unsigned numbers.
TEST(PrimitivesTest, ATickAndACommandSequenceAreSixtyFourBitUnsignedCounters) {
  EXPECT_TRUE((std::same_as<Tick, std::uint64_t>));
  EXPECT_TRUE((std::same_as<Sequence, std::uint64_t>));
}

// Parameters' Player count runs from 1 to kMaxPlayers (ADR-0043), and a client
// repeats at least one unacknowledged command per message.
TEST(PrimitivesTest, EveryBoundAdmitsAtLeastOne) {
  EXPECT_GE(kMaxPlayers, 1U);
  EXPECT_GE(kMaxCommandsPerMessage, 1U);
  EXPECT_GE(kMaxRecoilKicks, 1U);
}

}  // namespace
