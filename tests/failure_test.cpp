#include "augusta/failure.h"

#include <atomic>
#include <expected>
#include <stdexcept>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/faults.h"

// The shared error model (ADR-0033): a stable code classifies a failure into
// the scope it is recovered at, whatever its dependency said; a controlled
// fault stands in for a dependency failing, so a test can make one fail.
namespace {

using augusta::failure::Code;
using augusta::failure::DescribeFailure;
using augusta::failure::Disposition;
using augusta::failure::DispositionOf;
using augusta::failure::Failure;
using augusta::failure::Faults;
using augusta::failure::Guard;
using augusta::failure::InjectedFault;
using augusta::failure::Site;

TEST(FailureTest, EachCodeIsClassifiedIntoTheScopeItIsRecoveredAt) {
  EXPECT_EQ(DispositionOf(Code::kInvalidPeerInput), Disposition::kPeer);
  EXPECT_EQ(DispositionOf(Code::kPeerMisbehaving), Disposition::kSession);
  EXPECT_EQ(DispositionOf(Code::kPeerConnectionLost), Disposition::kSession);
  EXPECT_EQ(DispositionOf(Code::kJoinRefused), Disposition::kSession);
  EXPECT_EQ(DispositionOf(Code::kServerUnreachable), Disposition::kSession);
  EXPECT_EQ(DispositionOf(Code::kRecordingWriteFailed), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kRecordingFlushFailed), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kMetricsEndpointFailed), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kCaptureWriteFailed), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kCaptureFlushFailed), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kCaptureQueueFull), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kCaptureRecordTooLong), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kTransportInitFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kListenerSetupFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kTransportSendFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kTransportReceiveFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kWorkerCreationFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kWorkerFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kInvariantViolated), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kStrictRecordingFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kStrictCaptureFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kInvalidConfiguration), Disposition::kProcess);
  EXPECT_EQ(DispositionOf(Code::kInvalidContent), Disposition::kProcess);
  EXPECT_EQ(DispositionOf(Code::kDependencyInitFailed), Disposition::kProcess);
}

TEST(FailureTest, TheDescriptionNamesTheCodeScopeContextAndDetail) {
  const Failure failure{
      .code = Code::kTransportSendFailed,
      .context = {{.key = "session", .value = "3"}, {.key = "channel", .value = "reliable"}},
      .detail = "k_EResultNoConnection",
  };

  EXPECT_EQ(DescribeFailure(failure),
            "code=transport_send_failed disposition=runtime session=3 channel=reliable "
            "detail=\"k_EResultNoConnection\"");
}

TEST(FailureTest, TheDescriptionLeavesOutAnEmptyDetail) {
  const Failure failure{.code = Code::kInvalidPeerInput, .context = {}, .detail = {}};

  EXPECT_EQ(DescribeFailure(failure), "code=invalid_peer_input disposition=peer");
}

TEST(FailureTest, TheDescriptionQuotesWhatWouldBreakTheLine) {
  const Failure failure{
      .code = Code::kInvalidContent,
      .context = {{.key = "file", .value = "maps/old town.pack"}},
      .detail = "bad \"key\"\nCRIT forged=1 \\",
  };

  EXPECT_EQ(DescribeFailure(failure), R"(code=invalid_content disposition=process file="maps/old town.pack" )"
                                      R"(detail="bad \"key\"\nCRIT forged=1 \\")");
}

TEST(FailureTest, GuardPassesOnWhatTheBodyReturns) {
  const std::expected<int, Failure> result = Guard(Code::kDependencyInitFailed, [] { return 7; });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 7);
}

TEST(FailureTest, GuardTurnsADependencyExceptionIntoTheFailureKeepingWhatItSaid) {
  const std::expected<void, Failure> result =
      Guard(Code::kListenerSetupFailed, [] { throw std::runtime_error("address in use"); });

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, Code::kListenerSetupFailed);
  EXPECT_EQ(DispositionOf(result.error().code), Disposition::kRuntime);
  EXPECT_EQ(result.error().detail, "address in use");
}

TEST(FailureTest, GuardTurnsSomethingThrownThatIsNotAnExceptionIntoTheFailure) {
  constexpr int kNotAnException = 42;
  const std::expected<int, Failure> result = Guard(Code::kWorkerFailed, []() -> int { throw kNotAnException; });

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, Code::kWorkerFailed);
  EXPECT_EQ(result.error().detail, "unknown exception");
}

TEST(FaultsTest, AnUnarmedSiteNeverTrips) {
  Faults faults;

  EXPECT_FALSE(faults.Trip(Site::kTransportSend).has_value());
}

TEST(FaultsTest, AnArmedSiteTripsAsManyTimesAsItWasArmedFor) {
  Faults faults;
  faults.Arm(Site::kRecordingWrite, "disk full", 2);

  EXPECT_EQ(faults.Trip(Site::kRecordingWrite), "disk full");
  EXPECT_EQ(faults.Trip(Site::kRecordingWrite), "disk full");
  EXPECT_FALSE(faults.Trip(Site::kRecordingWrite).has_value());
}

TEST(FaultsTest, ArmingOneSiteLeavesTheOthersAlone) {
  Faults faults;
  faults.Arm(Site::kTransportSend, "no connection");

  EXPECT_FALSE(faults.Trip(Site::kTransportReceive).has_value());
  EXPECT_TRUE(faults.Trip(Site::kTransportSend).has_value());
}

TEST(FaultsTest, ASiteArmedEveryTimeKeepsTrippingUntilDisarmed) {
  Faults faults;
  faults.Arm(Site::kMetricsAccept, "socket closed", Faults::kEveryTime);

  for (int i = 0; i < 100; ++i) {
    ASSERT_EQ(faults.Trip(Site::kMetricsAccept), "socket closed");
  }
  faults.Disarm(Site::kMetricsAccept);

  EXPECT_FALSE(faults.Trip(Site::kMetricsAccept).has_value());
}

TEST(FaultsTest, ATrippedThrowStandsInForADependencyThatThrowsAndGuardClassifiesIt) {
  Faults faults;
  faults.Arm(Site::kDependencyInit, "GameNetworkingSockets_Init failed");

  const std::expected<void, Failure> result =
      Guard(Code::kTransportInitFailed, [&] { faults.ThrowIfTripped(Site::kDependencyInit); });

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, Code::kTransportInitFailed);
  EXPECT_EQ(result.error().detail, "GameNetworkingSockets_Init failed");
}

TEST(FaultsTest, AnUntrippedThrowDoesNothing) {
  Faults faults;

  EXPECT_NO_THROW(faults.ThrowIfTripped(Site::kWorkerExecution));
}

TEST(FaultsTest, TheThrownExceptionIsAnInjectedFault) {
  Faults faults;
  faults.Arm(Site::kWorkerCreation, "thread limit");

  EXPECT_THROW(faults.ThrowIfTripped(Site::kWorkerCreation), InjectedFault);
}

TEST(FaultsTest, ThreadsTrippingAtOnceShareTheArmedShotsExactly) {
  constexpr int kShots = 1000;
  constexpr int kThreads = 8;
  Faults faults;
  faults.Arm(Site::kTransportReceive, "connection reset", kShots);
  std::atomic<int> tripped{0};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < kShots; ++i) {
        if (faults.Trip(Site::kTransportReceive).has_value()) {
          tripped.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(tripped.load(), kShots);
}

}  // namespace
