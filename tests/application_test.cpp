#include "augusta/application.h"

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/failure.h"
#include "augusta/logging.h"

// An executable's application boundary (ADR-0033): whatever its startup or its
// runtime fails with, and however, it ends as one classified Outcome.
namespace {

using augusta::application::Conclude;
using augusta::application::Execute;
using augusta::application::Lifecycle;
using augusta::application::Outcome;
using augusta::failure::Code;
using augusta::failure::Failure;

// What a Lifecycle's phases did, in order, and when the runtime went.
using Trace = std::vector<std::string>;

// A runtime that says when it goes, so a test can see it released.
class FakeRuntime {
 public:
  explicit FakeRuntime(Trace& trace) : trace_(trace) {}
  ~FakeRuntime() { trace_.emplace_back("released"); }

  FakeRuntime(const FakeRuntime&) = delete;
  FakeRuntime& operator=(const FakeRuntime&) = delete;
  FakeRuntime(FakeRuntime&&) = delete;
  FakeRuntime& operator=(FakeRuntime&&) = delete;

 private:
  Trace& trace_;
};

// A Lifecycle whose phases all succeed and record themselves in trace; a test
// replaces the phase it makes fail.
Lifecycle<FakeRuntime> Succeeding(Trace& trace) {
  return {
      .initialize = [&trace]() -> std::expected<void, Failure> {
        trace.emplace_back("initialize");
        return {};
      },
      .construct = [&trace]() -> std::expected<std::unique_ptr<FakeRuntime>, Failure> {
        trace.emplace_back("construct");
        return std::make_unique<FakeRuntime>(trace);
      },
      .run = [&trace](FakeRuntime& /*runtime*/) -> Outcome {
        trace.emplace_back("run");
        return std::nullopt;
      },
  };
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

TEST(ApplicationTest, ARuntimeFailureReachesTheOutcomeWithItsTypedCause) {
  Trace trace;
  Lifecycle<FakeRuntime> lifecycle = Succeeding(trace);
  lifecycle.run = [](FakeRuntime& /*runtime*/) -> Outcome {
    return Failure{
        .code = Code::kTransportSendFailed, .context = {{.key = "thread", .value = "network"}}, .detail = "x"};
  };

  const Outcome outcome = Execute(lifecycle);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kTransportSendFailed);
  EXPECT_EQ(ContextOf(*outcome, "thread"), "network");
  EXPECT_EQ(outcome->detail, "x");
}

TEST(ApplicationTest, AnInitializationFailureIsTheOutcomeAndNothingIsConstructed) {
  Trace trace;
  Lifecycle<FakeRuntime> lifecycle = Succeeding(trace);
  lifecycle.initialize = [&trace]() -> std::expected<void, Failure> {
    trace.emplace_back("initialize");
    return std::unexpected(Failure{.code = Code::kTransportInitFailed, .context = {}, .detail = "no sockets"});
  };

  const Outcome outcome = Execute(lifecycle);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kTransportInitFailed);
  EXPECT_EQ(trace, (Trace{"initialize"}));
}

TEST(ApplicationTest, AConstructionFailureIsTheOutcomeAndNothingRuns) {
  Trace trace;
  Lifecycle<FakeRuntime> lifecycle = Succeeding(trace);
  lifecycle.construct = [&trace]() -> std::expected<std::unique_ptr<FakeRuntime>, Failure> {
    trace.emplace_back("construct");
    return std::unexpected(Failure{.code = Code::kInvalidContent, .context = {}, .detail = "no map"});
  };

  const Outcome outcome = Execute(lifecycle);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidContent);
  EXPECT_EQ(trace, (Trace{"initialize", "construct"}));
}

TEST(ApplicationTest, AnExceptionEscapingInitializationIsAClassifiedDependencyFailure) {
  Trace trace;
  Lifecycle<FakeRuntime> lifecycle = Succeeding(trace);
  lifecycle.initialize = []() -> std::expected<void, Failure> { throw std::runtime_error("GameNetworkingSockets"); };

  const Outcome outcome = Execute(lifecycle);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kDependencyInitFailed);
  EXPECT_EQ(ContextOf(*outcome, "phase"), "initialize");
  EXPECT_EQ(outcome->detail, "GameNetworkingSockets");
  EXPECT_TRUE(trace.empty());
}

TEST(ApplicationTest, AnExceptionEscapingConstructionIsAClassifiedDependencyFailure) {
  Trace trace;
  Lifecycle<FakeRuntime> lifecycle = Succeeding(trace);
  lifecycle.construct = []() -> std::expected<std::unique_ptr<FakeRuntime>, Failure> {
    throw std::runtime_error("failed to bind 127.0.0.1:1");
  };

  const Outcome outcome = Execute(lifecycle);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kDependencyInitFailed);
  EXPECT_EQ(ContextOf(*outcome, "phase"), "construct");
  EXPECT_EQ(outcome->detail, "failed to bind 127.0.0.1:1");
  EXPECT_EQ(trace, (Trace{"initialize"}));
}

TEST(ApplicationTest, AnExceptionEscapingTheRunIsAClassifiedWorkerFailureAfterWhichTheRuntimeIsReleased) {
  Trace trace;
  Lifecycle<FakeRuntime> lifecycle = Succeeding(trace);
  lifecycle.run = [&trace](FakeRuntime& /*runtime*/) -> Outcome {
    trace.emplace_back("run");
    throw std::runtime_error("device removed");
  };

  const Outcome outcome = Execute(lifecycle);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kWorkerFailed);
  EXPECT_EQ(ContextOf(*outcome, "phase"), "run");
  EXPECT_EQ(outcome->detail, "device removed");
  EXPECT_EQ(trace, (Trace{"initialize", "construct", "run", "released"}));
}

TEST(ApplicationTest, ARuntimeThatStopsAsAskedEndsWithNoFailureOnceItIsReleased) {
  Trace trace;

  const Outcome outcome = Execute(Succeeding(trace));

  EXPECT_FALSE(outcome.has_value());
  EXPECT_EQ(trace, (Trace{"initialize", "construct", "run", "released"}));
}

// How many times needle occurs in text.
std::size_t Occurrences(std::string_view text, std::string_view needle) {
  std::size_t count = 0;
  for (std::size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

// What Conclude writes for outcome, and the exit status it returns.
std::pair<std::string, int> Concluded(const Outcome& outcome) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  testing::internal::CaptureStdout();
  const int status = Conclude("server", outcome);
  return {testing::internal::GetCapturedStdout(), status};
}

TEST(ApplicationTest, ATerminalFailureWritesOneTerminalEventAndExitsNonZero) {
  const Failure failure{.code = Code::kWorkerFailed, .context = {{.key = "thread", .value = "network"}}, .detail = "x"};

  const auto [written, status] = Concluded(failure);

  EXPECT_NE(status, 0);
  EXPECT_EQ(Occurrences(written, "\n"), 1U) << written;
  EXPECT_EQ(Occurrences(written,
                        "CRITICAL subsystem=server event=terminal_failure code=worker_failed "
                        "disposition=runtime thread=network detail=\"x\""),
            1U)
      << written;
}

TEST(ApplicationTest, EveryDispositionOfTerminalFailureExitsNonZero) {
  for (const Code code : {Code::kPeerConnectionLost, Code::kRecordingWriteFailed, Code::kTransportInitFailed,
                          Code::kInvalidConfiguration}) {
    EXPECT_NE(Concluded(Failure{.code = code, .context = {}, .detail = {}}).second, 0);
  }
}

TEST(ApplicationTest, AStopAsAskedExitsZeroWithoutAnErrorLine) {
  const auto [written, status] = Concluded(std::nullopt);

  EXPECT_EQ(status, 0);
  EXPECT_EQ(Occurrences(written, "CRITICAL"), 0U) << written;
  EXPECT_EQ(Occurrences(written, "ERROR"), 0U) << written;
  EXPECT_EQ(Occurrences(written, "subsystem=server event=exiting exit_status=0"), 1U) << written;
}

}  // namespace
