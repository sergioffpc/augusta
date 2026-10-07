#include "augusta/supervisor.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "augusta/failure.h"
#include "augusta/faults.h"

// The runtime supervisor (ADR-0005): it owns its workers' lifetimes, the one
// stop request they all watch, and the first terminal failure any of them hits,
// as a typed failure::Failure (ADR-0033).
namespace {

using augusta::failure::Code;
using augusta::failure::DescribeFailure;
using augusta::failure::Disposition;
using augusta::failure::DispositionOf;
using augusta::failure::Failure;
using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::supervisor::State;
using augusta::supervisor::Supervisor;

// Waits, polling, until stop is requested; what a worker's loop does.
void UntilStopped(const Supervisor& supervisor) {
  while (!supervisor.StopRequested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

// The value of the failure's thread context, or empty if it names none.
std::string ThreadOf(const Failure& failure) {
  for (const auto& field : failure.context) {
    if (field.key == "thread") {
      return field.value;
    }
  }
  return {};
}

TEST(SupervisorTest, NothingHasFailedAndNoStopIsRequestedAtFirst) {
  const Supervisor supervisor;

  EXPECT_EQ(supervisor.GetState(), State::kRunning);
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
  EXPECT_EQ(supervisor.GetState(), State::kStopped);
  EXPECT_FALSE(supervisor.Failure().has_value());
}

TEST(SupervisorTest, AWorkerThatThrowsIsTheTypedFirstCauseAndStopsTheOthers) {
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
  EXPECT_EQ(supervisor.GetState(), State::kStopped);
  const std::optional<Failure> failure = supervisor.Failure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, Code::kWorkerFailed);
  EXPECT_EQ(DispositionOf(failure->code), Disposition::kRuntime);
  EXPECT_EQ(ThreadOf(*failure), "network");
  EXPECT_EQ(failure->detail, "address rejected");
}

TEST(SupervisorTest, AFailureWhileStoppingDoesNotReplaceTheFirstCause) {
  Supervisor supervisor;
  supervisor.Spawn("network", [&] {
    UntilStopped(supervisor);
    throw std::runtime_error("second");
  });

  supervisor.Run("simulation", [] { throw std::runtime_error("first"); });
  supervisor.StopAndJoin();

  const std::optional<Failure> failure = supervisor.Failure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(ThreadOf(*failure), "simulation");
  EXPECT_EQ(failure->detail, "first");
}

TEST(SupervisorTest, SomethingThrownThatIsNotAnExceptionIsStillAFailure) {
  Supervisor supervisor;
  constexpr int kNotAnException = 42;
  supervisor.Run("simulation", [] { throw kNotAnException; });

  ASSERT_TRUE(supervisor.Failure().has_value());
  EXPECT_EQ(supervisor.Failure()->code, Code::kWorkerFailed);
  EXPECT_EQ(supervisor.Failure()->detail, "unknown exception");
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
  EXPECT_EQ(supervisor.GetState(), State::kStopping);
  supervisor.StopAndJoin();

  EXPECT_EQ(supervisor.GetState(), State::kStopped);
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

TEST(SupervisorTest, NoWorkIsAdmittedOnceATerminalFailureHasRequestedTheStop) {
  Supervisor supervisor;
  supervisor.Run("simulation", [] { throw std::runtime_error("authority lost"); });
  std::atomic<bool> spawned_ran{false};
  bool run_ran = false;

  supervisor.Spawn("network", [&] { spawned_ran = true; });
  supervisor.Run("prediction", [&] { run_ran = true; });
  supervisor.StopAndJoin();

  EXPECT_FALSE(spawned_ran);
  EXPECT_FALSE(run_ran);
  EXPECT_EQ(supervisor.GetState(), State::kStopped);
  ASSERT_TRUE(supervisor.Failure().has_value());
  EXPECT_EQ(supervisor.Failure()->detail, "authority lost");
}

TEST(SupervisorTest, AWorkerThatCannotBeCreatedIsTheFirstCauseAndTheOthersAreStoppedAndJoined) {
  Faults faults;
  Supervisor supervisor(faults);
  std::atomic<bool> other_finished{false};
  supervisor.Spawn("prediction", [&] {
    UntilStopped(supervisor);
    other_finished = true;
  });
  faults.Arm(Site::kWorkerCreation, "resource temporarily unavailable");
  std::atomic<bool> ran{false};

  supervisor.Spawn("network", [&] { ran = true; });

  EXPECT_TRUE(supervisor.StopRequested());
  supervisor.StopAndJoin();
  EXPECT_TRUE(other_finished);
  EXPECT_FALSE(ran);
  EXPECT_EQ(supervisor.GetState(), State::kStopped);
  const std::optional<Failure> failure = supervisor.Failure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, Code::kWorkerCreationFailed);
  EXPECT_EQ(DispositionOf(failure->code), Disposition::kRuntime);
  EXPECT_EQ(ThreadOf(*failure), "network");
  EXPECT_EQ(failure->detail, "resource temporarily unavailable");
}

TEST(SupervisorTest, AWorkerMadeToFailWhileRunningIsTheFirstCause) {
  Faults faults;
  Supervisor supervisor(faults);
  faults.Arm(Site::kWorkerExecution, "injected");
  bool ticked = false;

  supervisor.Run("simulation", [&] { ticked = true; });

  EXPECT_FALSE(ticked);
  EXPECT_TRUE(supervisor.StopRequested());
  ASSERT_TRUE(supervisor.Failure().has_value());
  EXPECT_EQ(supervisor.Failure()->code, Code::kWorkerFailed);
  EXPECT_EQ(supervisor.Failure()->detail, "injected");
}

TEST(SupervisorTest, TheFirstCauseIsDescribedWithItsThread) {
  Supervisor supervisor;
  supervisor.Run("network", [] { throw std::runtime_error("address rejected"); });

  ASSERT_TRUE(supervisor.Failure().has_value());
  EXPECT_EQ(DescribeFailure(*supervisor.Failure()),
            "code=worker_failed disposition=runtime thread=network detail=\"address rejected\"");
}

}  // namespace
