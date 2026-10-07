#include "augusta/primitives.h"

#include <concepts>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

// Links augusta_primitives alone, so this file compiles only while the header
// includes nothing of the engine's: the module's whole point.
namespace {

using augusta::primitives::kMaxCommandsPerMessage;
using augusta::primitives::kMaxPlayers;
using augusta::primitives::kMaxRecoilKicks;
using augusta::primitives::Sequence;
using augusta::primitives::Tick;

// ADR-0038: a tick and a command sequence travel in 8 bytes and never wrap,
// so every receiver orders them as plain unsigned numbers.
TEST(PrimitivesTest, ATickAndACommandSequenceAreSixtyFourBitUnsignedCounters) {
  EXPECT_TRUE((std::same_as<Tick, std::uint64_t>));
  EXPECT_TRUE((std::same_as<Sequence, std::uint64_t>));
}

TEST(PrimitivesTest, TheBoundsAreTheProtocolsCurrentLimits) {
  EXPECT_EQ(kMaxPlayers, 8U);
  EXPECT_EQ(kMaxCommandsPerMessage, 8U);
  EXPECT_EQ(kMaxRecoilKicks, 64U);
}

// A list travels as a one-byte length and its elements (ADR-0038), so no bound
// on one may exceed what that byte counts.
TEST(PrimitivesTest, EveryListBoundFitsTheOneByteLengthItTravelsAs) {
  constexpr auto kOneByte = std::numeric_limits<std::uint8_t>::max();
  EXPECT_LE(kMaxPlayers, kOneByte);
  EXPECT_LE(kMaxCommandsPerMessage, kOneByte);
  EXPECT_LE(kMaxRecoilKicks, kOneByte);
}

// Parameters' Player count runs from 1 to kMaxPlayers (ADR-0043), and a client
// repeats at least one unacknowledged command per message.
TEST(PrimitivesTest, EveryBoundAdmitsAtLeastOne) {
  EXPECT_GE(kMaxPlayers, 1U);
  EXPECT_GE(kMaxCommandsPerMessage, 1U);
  EXPECT_GE(kMaxRecoilKicks, 1U);
}

}  // namespace
