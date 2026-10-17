#include "augusta/limits.h"

#include <gtest/gtest.h>

// Links augusta_limits alone, so this file compiles only while the header
// includes nothing of the engine's: the module's whole point.
namespace {

using augusta::limits::kMaxPlayers;
using augusta::limits::kMaxRecoilKicks;

// Parameters' Player count runs from 1 to kMaxPlayers (ADR-0043), and a recoil
// pattern may hold at least one kick.
TEST(LimitsTest, EveryBoundAdmitsAtLeastOne) {
  EXPECT_GE(kMaxPlayers, 1U);
  EXPECT_GE(kMaxRecoilKicks, 1U);
}

}  // namespace
