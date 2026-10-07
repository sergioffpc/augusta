#include "augusta/supervisor.h"

#include <atomic>
#include <chrono>
#include <expected>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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
using augusta::supervisor::kThreadContextKey;
using augusta::supervisor::State;
using augusta::supervisor::Supervisor;
using augusta::supervisor::WorkerResult;

// Waits, polling, until stop is requested; what a worker's loop does.
WorkerResult UntilStopped(const Supervisor& supervisor) {
  while (!supervisor.StopRequested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return {};
}

// The value of the failure's context under key, or empty if it has none.
std::string ContextOf(const Failure& failure, std::string_view key) {
  for (const auto& field : failure.context) {
    if (field.key == key) {
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
    return WorkerResult{};
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
    return WorkerResult{};
  });
  supervisor.Spawn("network", []() -> WorkerResult { throw std::runtime_error("address rejected"); });

  UntilStopped(supervisor);
  supervisor.StopAndJoin();

  EXPECT_TRUE(other_stopped);
  EXPECT_EQ(supervisor.GetState(), State::kStopped);
  const std::optional<Failure> failure = supervisor.Failure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, Code::kWorkerFailed);
  EXPECT_EQ(DispositionOf(failure->code), Disposition::kRuntime);
  EXPECT_EQ(ContextOf(*failure, kThreadContextKey), "network");
  EXPECT_EQ(failure->detail, "address rejected");
}

TEST(SupervisorTest, AFailureWhileStoppingDoesNotReplaceTheFirstCause) {
  Supervisor supervisor;
  supervisor.Spawn("network", [&]() -> WorkerResult {
    UntilStopped(supervisor);
    throw std::runtime_error("second");
  });

  supervisor.Run("simulation", []() -> WorkerResult { throw std::runtime_error("first"); });
  supervisor.StopAndJoin();

  const std::optional<Failure> failure = supervisor.Failure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(ContextOf(*failure, kThreadContextKey), "simulation");
  EXPECT_EQ(failure->detail, "first");
}

TEST(SupervisorTest, AFailureAWorkerReturnsIsTheFirstCauseWithItsOwnCodeAndContext) {
  Supervisor supervisor;
  std::atomic<bool> other_stopped{false};
  supervisor.Spawn("simulation", [&] {
    UntilStopped(supervisor);
    other_stopped = true;
    return WorkerResult{};
  });

  supervisor.Run("network", []() -> WorkerResult {
    return std::unexpected(Failure{
        .code = Code::kTransportSendFailed, .context = {{.key = "session", .value = "3"}}, .detail = "refused"});
  });
  supervisor.StopAndJoin();

  EXPECT_TRUE(other_stopped);
  const std::optional<Failure> failure = supervisor.Failure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, Code::kTransportSendFailed);
  EXPECT_EQ(DispositionOf(failure->code), Disposition::kRuntime);
  EXPECT_EQ(ContextOf(*failure, "session"), "3");
  EXPECT_EQ(ContextOf(*failure, kThreadContextKey), "network");
  EXPECT_EQ(failure->detail, "refused");
}

TEST(SupervisorTest, AFailureAfterAStopFromOutsideIsStillTheFirstCause) {
  Supervisor supervisor;
  supervisor.Spawn("network", [&]() -> WorkerResult {
    UntilStopped(supervisor);
    throw std::runtime_error("disconnect failed");
  });

  supervisor.RequestStop();
  supervisor.StopAndJoin();

  ASSERT_TRUE(supervisor.Failure().has_value());
  EXPECT_EQ(ContextOf(*supervisor.Failure(), kThreadContextKey), "network");
  EXPECT_EQ(supervisor.Failure()->detail, "disconnect failed");
}

TEST(SupervisorTest, SomethingThrownThatIsNotAnExceptionIsStillAFailure) {
  Supervisor supervisor;
  constexpr int kNotAnException = 42;
  supervisor.Run("simulation", []() -> WorkerResult { throw kNotAnException; });

  ASSERT_TRUE(supervisor.Failure().has_value());
  EXPECT_EQ(supervisor.Failure()->code, Code::kWorkerFailed);
  EXPECT_EQ(supervisor.Failure()->detail, "unknown exception");
  EXPECT_TRUE(supervisor.StopRequested());
}

TEST(SupervisorTest, RunOnTheCallingThreadReturnsWhenItsBodyDoes) {
  Supervisor supervisor;
  int runs = 0;

  supervisor.Run("simulation", [&] {
    ++runs;
    return WorkerResult{};
  });

  EXPECT_EQ(runs, 1);
  EXPECT_FALSE(supervisor.StopRequested());
  EXPECT_FALSE(supervisor.Failure().has_value());
}

TEST(SupervisorTest, AStopRequestedFromOutsideStopsEveryWorkerWithoutAFailure) {
  Supervisor supervisor;
  supervisor.Spawn("network", [&] { return UntilStopped(supervisor); });

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
      return WorkerResult{};
    });
  }

  EXPECT_TRUE(finished);
}

TEST(SupervisorTest, NoWorkIsAdmittedOnceATerminalFailureHasRequestedTheStop) {
  Supervisor supervisor;
  supervisor.Run("simulation", []() -> WorkerResult { throw std::runtime_error("authority lost"); });
  std::atomic<bool> spawned_ran{false};
  bool run_ran = false;

  supervisor.Spawn("network", [&] {
    spawned_ran = true;
    return WorkerResult{};
  });
  supervisor.Run("prediction", [&] {
    run_ran = true;
    return WorkerResult{};
  });
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
    return WorkerResult{};
  });
  faults.Arm(Site::kWorkerCreation, "resource temporarily unavailable");
  std::atomic<bool> ran{false};

  supervisor.Spawn("network", [&] {
    ran = true;
    return WorkerResult{};
  });

  EXPECT_TRUE(supervisor.StopRequested());
  supervisor.StopAndJoin();
  EXPECT_TRUE(other_finished);
  EXPECT_FALSE(ran);
  EXPECT_EQ(supervisor.GetState(), State::kStopped);
  const std::optional<Failure> failure = supervisor.Failure();
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, Code::kWorkerCreationFailed);
  EXPECT_EQ(DispositionOf(failure->code), Disposition::kRuntime);
  EXPECT_EQ(ContextOf(*failure, kThreadContextKey), "network");
  EXPECT_EQ(failure->detail, "resource temporarily unavailable");
}

TEST(SupervisorTest, AWorkerMadeToFailWhileRunningIsTheFirstCause) {
  Faults faults;
  Supervisor supervisor(faults);
  faults.Arm(Site::kWorkerExecution, "injected");
  bool ticked = false;

  supervisor.Run("simulation", [&] {
    ticked = true;
    return WorkerResult{};
  });

  EXPECT_FALSE(ticked);
  EXPECT_TRUE(supervisor.StopRequested());
  ASSERT_TRUE(supervisor.Failure().has_value());
  EXPECT_EQ(supervisor.Failure()->code, Code::kWorkerFailed);
  EXPECT_EQ(supervisor.Failure()->detail, "injected");
}

TEST(SupervisorTest, TheFirstCauseIsDescribedWithItsThread) {
  Supervisor supervisor;
  supervisor.Run("network", []() -> WorkerResult { throw std::runtime_error("address rejected"); });

  ASSERT_TRUE(supervisor.Failure().has_value());
  EXPECT_EQ(DescribeFailure(*supervisor.Failure()),
            "code=worker_failed disposition=runtime thread=network detail=\"address rejected\"");
}

}  // namespace
