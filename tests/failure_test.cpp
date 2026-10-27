#include "augusta/failure.h"

#include <expected>
#include <optional>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "augusta/failure_latch.h"

// The shared error model (ADR-0033): a stable code classifies a failure into
// the scope it is recovered at, whatever its dependency said.
namespace {

using augusta::failure::ClassifiedFailure;
using augusta::failure::Code;
using augusta::failure::DescribeFailure;
using augusta::failure::Disposition;
using augusta::failure::DispositionOf;
using augusta::failure::Failure;
using augusta::failure::FailureLatch;
using augusta::failure::Guard;

// Requirements: NFR-18
TEST(FailureTest, EachCodeIsClassifiedIntoTheScopeItIsRecoveredAt) {
  EXPECT_EQ(DispositionOf(Code::kInvalidPeerInput), Disposition::kPeer);
  EXPECT_EQ(DispositionOf(Code::kPeerMisbehaving), Disposition::kSession);
  EXPECT_EQ(DispositionOf(Code::kPeerConnectionLost), Disposition::kSession);
  EXPECT_EQ(DispositionOf(Code::kJoinRefused), Disposition::kSession);
  EXPECT_EQ(DispositionOf(Code::kServerUnreachable), Disposition::kSession);
  EXPECT_EQ(DispositionOf(Code::kMetricsEndpointFailed), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kCaptureWriteFailed), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kCaptureFlushFailed), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kCaptureQueueFull), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kCaptureRecordTooLong), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kCaptureRetentionBudget), Disposition::kSubsystem);
  EXPECT_EQ(DispositionOf(Code::kTransportInitFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kListenerSetupFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kTransportSendFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kTransportReceiveFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kWorkerCreationFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kWorkerFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kInvariantViolated), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kStrictCaptureFailed), Disposition::kRuntime);
  EXPECT_EQ(DispositionOf(Code::kInvalidConfiguration), Disposition::kProcess);
  EXPECT_EQ(DispositionOf(Code::kInvalidContent), Disposition::kProcess);
  EXPECT_EQ(DispositionOf(Code::kDependencyInitFailed), Disposition::kProcess);
}

// Requirements: NFR-18
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

// Requirements: NFR-18
TEST(FailureTest, TheDescriptionLeavesOutAnEmptyDetail) {
  const Failure failure{.code = Code::kInvalidPeerInput, .context = {}, .detail = {}};

  EXPECT_EQ(DescribeFailure(failure), "code=invalid_peer_input disposition=peer");
}

// Requirements: NFR-18
TEST(FailureTest, TheDescriptionQuotesWhatWouldBreakTheLine) {
  const Failure failure{
      .code = Code::kInvalidContent,
      .context = {{.key = "file", .value = "maps/old town.apak"}},
      .detail = "bad \"key\"\nCRIT forged=1 \\",
  };

  EXPECT_EQ(DescribeFailure(failure), R"(code=invalid_content disposition=process file="maps/old town.apak" )"
                                      R"(detail="bad \"key\"\nCRIT forged=1 \\")");
}

// Requirements: NFR-18
TEST(FailureTest, GuardPassesOnWhatTheBodyReturns) {
  const std::expected<int, Failure> result = Guard(Code::kDependencyInitFailed, [] { return 7; });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 7);
}

// Requirements: NFR-18
TEST(FailureTest, GuardTurnsADependencyExceptionIntoTheFailureKeepingWhatItSaid) {
  const std::expected<void, Failure> result =
      Guard(Code::kListenerSetupFailed, [] { throw std::runtime_error("address in use"); });

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, Code::kListenerSetupFailed);
  EXPECT_EQ(DispositionOf(result.error().code), Disposition::kRuntime);
  EXPECT_EQ(result.error().detail, "address in use");
}

// Startup code that has already classified its failure throws it whole: Guard
// keeps its Code, context and detail instead of taking it for a dependency's.
// Requirements: NFR-18
TEST(FailureTest, GuardKeepsAClassifiedFailureThrownWhole) {
  const Failure thrown{.code = Code::kInvariantViolated,
                       .context = {{.key = "record_type", .value = "header"}},
                       .detail = "string too long"};
  const std::expected<void, Failure> result =
      Guard(Code::kDependencyInitFailed, [&] { throw ClassifiedFailure(thrown); });

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, Code::kInvariantViolated);
  ASSERT_EQ(result.error().context.size(), 1U);
  EXPECT_EQ(result.error().context.front().key, "record_type");
  EXPECT_EQ(result.error().context.front().value, "header");
  EXPECT_EQ(result.error().detail, "string too long");
}

// Caught as any other exception, it still says what failed.
// Requirements: NFR-18
TEST(FailureTest, AClassifiedFailureThrownDescribesItself) {
  const Failure thrown{.code = Code::kInvariantViolated, .context = {}, .detail = "string too long"};
  EXPECT_EQ(std::string(ClassifiedFailure(thrown).what()), DescribeFailure(thrown));
}

// Requirements: NFR-18
TEST(FailureTest, GuardTurnsSomethingThrownThatIsNotAnExceptionIntoTheFailure) {
  constexpr int kNotAnException = 42;
  const std::expected<int, Failure> result = Guard(Code::kWorkerFailed, []() -> int { throw kNotAnException; });

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, Code::kWorkerFailed);
  EXPECT_EQ(result.error().detail, "unknown exception");
}

// Requirements: NFR-18
TEST(FailureLatchTest, OnlyTheFirstFailureIsKeptAndTakenOnce) {
  FailureLatch latch;
  latch.Record(Failure{.code = Code::kTransportSendFailed, .context = {}, .detail = {}});
  latch.Record(Failure{.code = Code::kInvariantViolated, .context = {}, .detail = {}});

  const std::optional<Failure> taken = latch.Take();

  ASSERT_TRUE(taken.has_value());
  EXPECT_EQ(taken->code, Code::kTransportSendFailed);
  EXPECT_FALSE(latch.Take().has_value());
}

// What keeps it goes on knowing it has failed after a worker took the failure.
// Requirements: NFR-18
TEST(FailureLatchTest, AFailureStaysRecordedAfterItIsTaken) {
  FailureLatch latch;
  EXPECT_FALSE(latch.Recorded());

  latch.Record(Failure{.code = Code::kTransportSendFailed, .context = {}, .detail = {}});
  EXPECT_TRUE(latch.Recorded());
  static_cast<void>(latch.Take());

  EXPECT_TRUE(latch.Recorded());
}

}  // namespace
