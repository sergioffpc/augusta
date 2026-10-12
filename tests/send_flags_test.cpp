#include "send_flags.h"

#include <gtest/gtest.h>
#include <steam/steamclientpublic.h>
#include <steam/steamnetworkingtypes.h>

#include "augusta/networking.h"

namespace {

using augusta::networking::ClassifySend;
using augusta::networking::Reliability;
using augusta::networking::SendFlags;
using augusta::networking::SendVerdict;

TEST(SendFlagsTest, UnreliableMessagesSkipNagle) {
  EXPECT_NE(SendFlags(Reliability::kUnreliable) & k_nSteamNetworkingSend_NoNagle, 0);
}

TEST(SendFlagsTest, UnreliableMessagesAreSentUnreliably) {
  EXPECT_EQ(SendFlags(Reliability::kUnreliable) & k_nSteamNetworkingSend_Reliable, 0);
}

TEST(SendFlagsTest, ReliableMessagesAreSentReliably) {
  EXPECT_NE(SendFlags(Reliability::kReliable) & k_nSteamNetworkingSend_Reliable, 0);
}

TEST(ClassifySendTest, AMessageTheTransportTookIsAccepted) {
  EXPECT_EQ(ClassifySend(k_EResultOK, Reliability::kReliable), SendVerdict::kAccepted);
  EXPECT_EQ(ClassifySend(k_EResultOK, Reliability::kUnreliable), SendVerdict::kAccepted);
}

// A connection that has ended or is ending is the peer's outcome, never a
// local failure: one client going away must not stop the server.
TEST(ClassifySendTest, AConnectionThatCannotTakeAMessageDropsIt) {
  for (const EResult result : {k_EResultNoConnection, k_EResultInvalidState, k_EResultIgnored}) {
    EXPECT_EQ(ClassifySend(result, Reliability::kReliable), SendVerdict::kDropped) << result;
    EXPECT_EQ(ClassifySend(result, Reliability::kUnreliable), SendVerdict::kDropped) << result;
  }
}

TEST(ClassifySendTest, AFullQueueDropsAnUnreliableMessage) {
  EXPECT_EQ(ClassifySend(k_EResultLimitExceeded, Reliability::kUnreliable), SendVerdict::kDropped);
}

// Reliable delivery is the transport's: with no room for a reliable message
// there is no retrying it, so the peer's connection ends instead.
TEST(ClassifySendTest, AFullQueueEndsTheConnectionOfAReliableMessage) {
  EXPECT_EQ(ClassifySend(k_EResultLimitExceeded, Reliability::kReliable), SendVerdict::kDroppedEndingConnection);
}

TEST(ClassifySendTest, AnyOtherResultIsALocalTransportFailure) {
  for (const EResult result : {k_EResultFail, k_EResultInvalidParam}) {
    EXPECT_EQ(ClassifySend(result, Reliability::kReliable), SendVerdict::kLocalFailure) << result;
    EXPECT_EQ(ClassifySend(result, Reliability::kUnreliable), SendVerdict::kLocalFailure) << result;
  }
}

}  // namespace
