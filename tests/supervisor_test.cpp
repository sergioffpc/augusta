#include "augusta/supervisor.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

// The runtime supervisor (ADR-0005): it owns its workers' lifetimes, the one
// stop request they all watch, and the first terminal error any of them hits.
namespace {

using augusta::supervisor::DescribeWorkerFailure;
using augusta::supervisor::Supervisor;
using augusta::supervisor::WorkerFailure;

// Waits, polling, until stop is requested; what a worker's loop does.
void UntilStopped(const Supervisor& supervisor) {
  while (!supervisor.StopRequested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

TEST(SupervisorTest, NothingHasFailedAndNoStopIsRequestedAtFirst) {
  const Supervisor supervisor;

  EXPECT_FALSE(supervisor.StopRequested());
  EXPECT_FALSE(supervisor.Failure().has_value());
}

TEST(SupervisorTest, AWorkerRunsUntilStopIsRequestedAndIsJoined) {
  Supervisor supervisor;
  std::atomic<bool> finished{false};
  supervisor.Spawn("network", [&] {
    UntilStopped(supervisor);
    finished = true;
  });

  supervisor.StopAndJoin();

  EXPECT_TRUE(finished);
  EXPECT_FALSE(supervisor.Failure().has_value());
}

TEST(SupervisorTest, AWorkerThatThrowsIsRecordedAndStopsTheOthers) {
  Supervisor supervisor;
  std::atomic<bool> other_stopped{false};
  supervisor.Spawn("prediction", [&] {
    UntilStopped(supervisor);
    other_stopped = true;
  });
  supervisor.Spawn("network", [] { throw std::runtime_error("address rejected"); });

  UntilStopped(supervisor);
  supervisor.StopAndJoin();

  EXPECT_TRUE(other_stopped);
  const std::optional<WorkerFailure> failure = supervisor.Failure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->thread, "network");
  EXPECT_EQ(failure->reason, "address rejected");
}

TEST(SupervisorTest, OnlyTheFirstFailureIsKept) {
  Supervisor supervisor;
  supervisor.Run("simulation", [] { throw std::runtime_error("first"); });
  supervisor.Run("network", [] { throw std::runtime_error("second"); });

  ASSERT_TRUE(supervisor.Failure().has_value());
  EXPECT_EQ(supervisor.Failure()->thread, "simulation");
  EXPECT_EQ(supervisor.Failure()->reason, "first");
}

TEST(SupervisorTest, SomethingThrownThatIsNotAnExceptionIsStillAFailure) {
  Supervisor supervisor;
  constexpr int kNotAnException = 42;
  supervisor.Run("simulation", [] { throw kNotAnException; });

  ASSERT_TRUE(supervisor.Failure().has_value());
  EXPECT_EQ(supervisor.Failure()->reason, "unknown exception");
  EXPECT_TRUE(supervisor.StopRequested());
}

TEST(SupervisorTest, RunOnTheCallingThreadReturnsWhenItsBodyDoes) {
  Supervisor supervisor;
  int runs = 0;

  supervisor.Run("simulation", [&] { ++runs; });

  EXPECT_EQ(runs, 1);
  EXPECT_FALSE(supervisor.StopRequested());
  EXPECT_FALSE(supervisor.Failure().has_value());
}

TEST(SupervisorTest, AStopRequestedFromOutsideStopsEveryWorkerWithoutAFailure) {
  Supervisor supervisor;
  supervisor.Spawn("network", [&] { UntilStopped(supervisor); });

  supervisor.RequestStop();
  supervisor.StopAndJoin();

  EXPECT_FALSE(supervisor.Failure().has_value());
}

TEST(SupervisorTest, DestroyingItStopsAndJoinsItsWorkers) {
  std::atomic<bool> finished{false};
  {
    Supervisor supervisor;
    supervisor.Spawn("network", [&] {
      UntilStopped(supervisor);
      finished = true;
    });
  }

  EXPECT_TRUE(finished);
}

TEST(SupervisorTest, AFailureIsDescribedWithItsThreadAndReason) {
  EXPECT_EQ(DescribeWorkerFailure(WorkerFailure{.thread = "network", .reason = "address rejected"}),
            "the network thread failed: address rejected");
}

}  // namespace
