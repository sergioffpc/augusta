#include <gtest/gtest.h>

#include "augusta/link_types.h"
#include "augusta/networking.h"
#include "reliability.h"

// Which reliability a Development build's messages travel with (ADR-0038,
// ADR-0074), as link_transport_test.cpp has the rest's.
namespace {

using augusta::link::MessageType;
using augusta::link::ReliabilityOf;
using augusta::networking::Reliability;

// A lost one would leave the server sending a stream nobody looks at, or
// none to the overlay that asked for it.
// Requirements: US-30
TEST(ReliabilityOfDebugTest, ADebugSubscribeTravelsReliably) {
  EXPECT_EQ(ReliabilityOf(MessageType::kDebugSubscribe), Reliability::kReliable);
}

// Each is sent once, when its round's flight ends: a lost one is a round the
// overlay never shows.
// Requirements: US-30
TEST(ReliabilityOfDebugTest, AShotJudgementTravelsReliably) {
  EXPECT_EQ(ReliabilityOf(MessageType::kShotJudgement), Reliability::kReliable);
}

}  // namespace
