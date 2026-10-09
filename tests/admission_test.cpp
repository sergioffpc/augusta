#include "admission.h"

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/networking.h"

// The deadlines are pure: time is handed to them, and no peer is ever really disconnected here.
namespace {

using augusta::networking::PeerId;
using augusta::server::AdmissionDeadlines;
using augusta::server::kAdmissionDeadline;
using Clock = std::chrono::steady_clock;
using Peers = std::vector<PeerId>;

const Clock::time_point kStart{};
constexpr auto kInstant = std::chrono::milliseconds(1);
constexpr auto kFirst = static_cast<PeerId>(1);
constexpr auto kSecond = static_cast<PeerId>(2);

// Requirements: NFR-05
TEST(AdmissionDeadlinesTest, APeerNotAdmittedIsOverdueOnceTheDeadlinePassesAndNotBefore) {
  AdmissionDeadlines deadlines;
  deadlines.Connected(kFirst, kStart);

  EXPECT_EQ(deadlines.TakeOverdue(kStart + kAdmissionDeadline - kInstant), Peers{});
  EXPECT_EQ(deadlines.TakeOverdue(kStart + kAdmissionDeadline), Peers{kFirst});
}

// Requirements: NFR-05
TEST(AdmissionDeadlinesTest, AnOverduePeerIsTakenOnlyOnce) {
  AdmissionDeadlines deadlines;
  deadlines.Connected(kFirst, kStart);

  ASSERT_EQ(deadlines.TakeOverdue(kStart + kAdmissionDeadline), Peers{kFirst});
  EXPECT_EQ(deadlines.TakeOverdue(kStart + (2 * kAdmissionDeadline)), Peers{});
}

// Requirements: NFR-05
TEST(AdmissionDeadlinesTest, AnAdmittedPeerIsNeverOverdue) {
  AdmissionDeadlines deadlines;
  deadlines.Connected(kFirst, kStart);
  deadlines.Admitted(kFirst);

  EXPECT_EQ(deadlines.TakeOverdue(kStart + (2 * kAdmissionDeadline)), Peers{});
}

// Requirements: NFR-05
TEST(AdmissionDeadlinesTest, APeerThatLeftIsNeverOverdue) {
  AdmissionDeadlines deadlines;
  deadlines.Connected(kFirst, kStart);
  deadlines.Left(kFirst);

  EXPECT_EQ(deadlines.TakeOverdue(kStart + (2 * kAdmissionDeadline)), Peers{});
}

// Requirements: NFR-05
TEST(AdmissionDeadlinesTest, EachPeerHasItsOwnDeadline) {
  AdmissionDeadlines deadlines;
  deadlines.Connected(kFirst, kStart);
  deadlines.Connected(kSecond, kStart + kAdmissionDeadline);

  EXPECT_EQ(deadlines.TakeOverdue(kStart + kAdmissionDeadline), Peers{kFirst});
  EXPECT_EQ(deadlines.TakeOverdue(kStart + (2 * kAdmissionDeadline) - kInstant), Peers{});
  EXPECT_EQ(deadlines.TakeOverdue(kStart + (2 * kAdmissionDeadline)), Peers{kSecond});
}

// Requirements: NFR-05
TEST(AdmissionDeadlinesTest, ThePeersAdmittedOrGoneLeaveTheOthersDeadlinesAsTheyWere) {
  AdmissionDeadlines deadlines;
  deadlines.Connected(kFirst, kStart);
  deadlines.Connected(kSecond, kStart);
  deadlines.Admitted(kFirst);

  EXPECT_EQ(deadlines.TakeOverdue(kStart + kAdmissionDeadline), Peers{kSecond});
}

}  // namespace
