#include "send_flags.h"

#include <gtest/gtest.h>
#include <steam/steamnetworkingtypes.h>

#include "augusta/networking.h"

namespace {

using augusta::networking::Reliability;
using augusta::networking::SendFlags;

TEST(SendFlagsTest, UnreliableMessagesSkipNagle) {
  EXPECT_NE(SendFlags(Reliability::kUnreliable) & k_nSteamNetworkingSend_NoNagle, 0);
}

TEST(SendFlagsTest, UnreliableMessagesAreSentUnreliably) {
  EXPECT_EQ(SendFlags(Reliability::kUnreliable) & k_nSteamNetworkingSend_Reliable, 0);
}

TEST(SendFlagsTest, ReliableMessagesAreSentReliably) {
  EXPECT_NE(SendFlags(Reliability::kReliable) & k_nSteamNetworkingSend_Reliable, 0);
}

}  // namespace
