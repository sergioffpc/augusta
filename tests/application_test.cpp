#include "augusta/application.h"

#include <cstddef>
#include <expected>
#include <filesystem>
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

using augusta::application::CheckMimalloc;
using augusta::application::CheckMsvcRuntime;
using augusta::application::Conclude;
using augusta::application::DescribeMimallocError;
using augusta::application::DescribeMsvcRuntimeError;
using augusta::application::Execute;
using augusta::application::FileVersion;
using augusta::application::MimallocError;
using augusta::application::MimallocFacts;
using augusta::application::MsvcRuntimeError;
using augusta::application::MsvcRuntimeFacts;
using augusta::application::Outcome;
using augusta::application::Program;
using augusta::failure::ClassifiedFailure;
using augusta::failure::Code;
using augusta::failure::Failure;

// What a Program's phases did, in order, and when the runtime went.
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

// A Program whose phases all succeed and record themselves in trace; a test
// replaces the phase it makes fail.
Program<FakeRuntime> Succeeding(Trace& trace) {
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

// Requirements: NFR-18
TEST(ApplicationTest, ARuntimeFailureReachesTheOutcomeWithItsTypedCause) {
  Trace trace;
  Program<FakeRuntime> program = Succeeding(trace);
  program.run = [](FakeRuntime& /*runtime*/) -> Outcome {
    return Failure{
        .code = Code::kTransportSendFailed, .context = {{.key = "thread", .value = "network"}}, .detail = "x"};
  };

  const Outcome outcome = Execute(program);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kTransportSendFailed);
  EXPECT_EQ(ContextOf(*outcome, "thread"), "network");
  EXPECT_EQ(outcome->detail, "x");
}

// Requirements: NFR-18
TEST(ApplicationTest, AnInitializationFailureIsTheOutcomeAndNothingIsConstructed) {
  Trace trace;
  Program<FakeRuntime> program = Succeeding(trace);
  program.initialize = [&trace]() -> std::expected<void, Failure> {
    trace.emplace_back("initialize");
    return std::unexpected(Failure{.code = Code::kTransportInitFailed, .context = {}, .detail = "no sockets"});
  };

  const Outcome outcome = Execute(program);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kTransportInitFailed);
  EXPECT_EQ(trace, (Trace{"initialize"}));
}

// Requirements: NFR-18
TEST(ApplicationTest, AConstructionFailureIsTheOutcomeAndNothingRuns) {
  Trace trace;
  Program<FakeRuntime> program = Succeeding(trace);
  program.construct = [&trace]() -> std::expected<std::unique_ptr<FakeRuntime>, Failure> {
    trace.emplace_back("construct");
    return std::unexpected(Failure{.code = Code::kInvalidContent, .context = {}, .detail = "no map"});
  };

  const Outcome outcome = Execute(program);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvalidContent);
  EXPECT_EQ(trace, (Trace{"initialize", "construct"}));
}

// Requirements: NFR-18
TEST(ApplicationTest, AnExceptionEscapingInitializationIsAClassifiedDependencyFailure) {
  Trace trace;
  Program<FakeRuntime> program = Succeeding(trace);
  program.initialize = []() -> std::expected<void, Failure> { throw std::runtime_error("GameNetworkingSockets"); };

  const Outcome outcome = Execute(program);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kDependencyInitFailed);
  EXPECT_EQ(ContextOf(*outcome, "phase"), "initialize");
  EXPECT_EQ(outcome->detail, "GameNetworkingSockets");
  EXPECT_TRUE(trace.empty());
}

// Requirements: NFR-18
TEST(ApplicationTest, AnExceptionEscapingConstructionIsAClassifiedDependencyFailure) {
  Trace trace;
  Program<FakeRuntime> program = Succeeding(trace);
  program.construct = []() -> std::expected<std::unique_ptr<FakeRuntime>, Failure> {
    throw std::runtime_error("failed to bind 127.0.0.1:1");
  };

  const Outcome outcome = Execute(program);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kDependencyInitFailed);
  EXPECT_EQ(ContextOf(*outcome, "phase"), "construct");
  EXPECT_EQ(outcome->detail, "failed to bind 127.0.0.1:1");
  EXPECT_EQ(trace, (Trace{"initialize"}));
}

// Construction that has already classified its failure throws it whole, and it
// keeps its Code: a broken invariant is not taken for a dependency's failure.
// Requirements: NFR-18
TEST(ApplicationTest, AClassifiedFailureThrownDuringConstructionKeepsItsCode) {
  Trace trace;
  Program<FakeRuntime> program = Succeeding(trace);
  program.construct = []() -> std::expected<std::unique_ptr<FakeRuntime>, Failure> {
    throw ClassifiedFailure(Failure{.code = Code::kInvariantViolated, .context = {}, .detail = "string too long"});
  };

  const Outcome outcome = Execute(program);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kInvariantViolated);
  EXPECT_EQ(ContextOf(*outcome, "phase"), "construct");
  EXPECT_EQ(outcome->detail, "string too long");
}

// Requirements: NFR-18
TEST(ApplicationTest, AnExceptionEscapingTheRunIsAClassifiedWorkerFailureAfterWhichTheRuntimeIsReleased) {
  Trace trace;
  Program<FakeRuntime> program = Succeeding(trace);
  program.run = [&trace](FakeRuntime& /*runtime*/) -> Outcome {
    trace.emplace_back("run");
    throw std::runtime_error("device removed");
  };

  const Outcome outcome = Execute(program);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->code, Code::kWorkerFailed);
  EXPECT_EQ(ContextOf(*outcome, "phase"), "run");
  EXPECT_EQ(outcome->detail, "device removed");
  EXPECT_EQ(trace, (Trace{"initialize", "construct", "run", "released"}));
}

// Requirements: NFR-18
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

// Requirements: NFR-18
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

// A client's terminal event also says which file holds its Run's log, so the
// player knows what to send (ADR-0027); it is still the one terminal event.
// Requirements: NFR-18
TEST(ApplicationTest, ATerminalFailureWithARunLogGivesItsPathInTheOneTerminalEvent) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  const std::filesystem::path log = std::filesystem::path(u8"C:/augustac/logs") / u8"augustac-run-中文.log";
  testing::internal::CaptureStdout();

  const int status =
      Conclude("client", Failure{.code = Code::kServerUnreachable, .context = {}, .detail = "timed out"}, log);

  const std::string written = testing::internal::GetCapturedStdout();
  EXPECT_EQ(status, 1);
  EXPECT_EQ(Occurrences(written, "\n"), 1U) << written;
  const std::u8string utf8 = log.u8string();
  EXPECT_EQ(Occurrences(written,
                        "CRITICAL subsystem=client event=terminal_failure code=server_unreachable "
                        "disposition=session detail=\"timed out\" exit_status=1 log=\"" +
                            std::string(utf8.begin(), utf8.end()) + "\""),
            1U)
      << written;
}

// A stop as asked leaves nothing to send, so it names no log.
// Requirements: NFR-18
TEST(ApplicationTest, AStopAsAskedWithARunLogNamesNoLog) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  testing::internal::CaptureStdout();

  const int status = Conclude("client", std::nullopt, std::filesystem::path("logs/augustac-run.log"));

  const std::string written = testing::internal::GetCapturedStdout();
  EXPECT_EQ(status, 0);
  EXPECT_EQ(Occurrences(written, "log="), 0U) << written;
}

// Requirements: NFR-18
TEST(ApplicationTest, EveryDispositionOfTerminalFailureExitsNonZero) {
  for (const Code code : {Code::kPeerConnectionLost, Code::kMetricsEndpointFailed, Code::kTransportInitFailed,
                          Code::kInvalidConfiguration}) {
    EXPECT_NE(Concluded(Failure{.code = code, .context = {}, .detail = {}}).second, 0);
  }
}

// Requirements: NFR-18
TEST(ApplicationTest, AStopAsAskedExitsZeroWithoutAnErrorLine) {
  const auto [written, status] = Concluded(std::nullopt);

  EXPECT_EQ(status, 0);
  EXPECT_EQ(Occurrences(written, "CRITICAL"), 0U) << written;
  EXPECT_EQ(Occurrences(written, "ERROR"), 0U) << written;
  EXPECT_EQ(Occurrences(written, "subsystem=server event=exiting exit_status=0"), 1U) << written;
}

// mimalloc serves the process (ADR-0078): it redirected the CRT's malloc where
// it has to, and the block malloc handed out is in its heap.
// Requirements: NFR-18
TEST(ApplicationTest, MimallocServesAProcessItRedirectedAndAllocatesFor) {
  const MimallocFacts facts{.redirected = true, .block_in_heap = true, .version = 30503};

  EXPECT_TRUE(CheckMimalloc(facts).has_value());
}

// Redirection is judged first: without it no block can be mimalloc's.
// Requirements: NFR-18
TEST(ApplicationTest, AProcessMimallocDidNotRedirectIsRefusedWithOneLineSayingSo) {
  const MimallocFacts facts{.redirected = false, .block_in_heap = false, .version = 30503};

  const auto checked = CheckMimalloc(facts);

  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error(), MimallocError::kNotRedirected);
  EXPECT_EQ(DescribeMimallocError("augustad", checked.error()),
            "augustad: mimalloc does not serve this process: malloc is not redirected to it "
            "(mimalloc.dll must be the first DLL it loads, with mimalloc-redirect.dll beside it)");
}

// Requirements: NFR-18
TEST(ApplicationTest, AProcessWhoseBlockIsOutsideMimallocsHeapIsRefusedWithOneLineSayingSo) {
  const MimallocFacts facts{.redirected = true, .block_in_heap = false, .version = 30503};

  const auto checked = CheckMimalloc(facts);

  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error(), MimallocError::kBlockOutsideHeap);
  EXPECT_EQ(DescribeMimallocError("augustac", checked.error()),
            "augustac: mimalloc does not serve this process: a block malloc handed out is outside mimalloc's heap");
}

// The msvcp140.dll the redistributable of the pinned sysroot's toolset ships
// (ADR-0079), as versions.env pins it.
constexpr FileVersion kMinimumMsvcRuntime{.major = 14, .minor = 44, .build = 35211, .revision = 0};

// A runtime as new as the pinned sysroot's STL is what augustac is built for.
// Requirements: NFR-18
TEST(ApplicationTest, AnMsvcRuntimeOfTheMinimumVersionServesTheProcess) {
  const MsvcRuntimeFacts facts{.loaded = kMinimumMsvcRuntime, .minimum = kMinimumMsvcRuntime};

  EXPECT_TRUE(CheckMsvcRuntime(facts).has_value());
}

// A newer runtime serves it too, judged part by part: a later minor version
// is newer whatever its build number.
// Requirements: NFR-18
TEST(ApplicationTest, AnMsvcRuntimeNewerThanTheMinimumServesTheProcess) {
  const MsvcRuntimeFacts facts{.loaded = FileVersion{.major = 14, .minor = 50, .build = 35000, .revision = 0},
                               .minimum = kMinimumMsvcRuntime};

  EXPECT_TRUE(CheckMsvcRuntime(facts).has_value());
}

// The 17.10 STL's std::mutex crashes against an older msvcp140.dll, so one
// older than the minimum by a single part is refused, naming both versions and
// where to get the one to install.
// Requirements: NFR-18
TEST(ApplicationTest, AnMsvcRuntimeOlderThanTheMinimumIsRefusedWithOneLineNamingTheVersionToInstall) {
  const MsvcRuntimeFacts facts{.loaded = FileVersion{.major = 14, .minor = 44, .build = 35210, .revision = 9},
                               .minimum = kMinimumMsvcRuntime};

  const auto checked = CheckMsvcRuntime(facts);

  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error(), MsvcRuntimeError::kTooOld);
  EXPECT_EQ(DescribeMsvcRuntimeError("augustac", facts, checked.error()),
            "augustac: the Visual C++ runtime it loaded (msvcp140.dll 14.44.35210.9) is older than the "
            "14.44.35211.0 it needs: install the Visual C++ Redistributable (x64) 14.44.35211.0 or newer, from "
            "https://aka.ms/vc14/vc_redist.x64.exe");
}

// A runtime whose version cannot be read may be any, so it is refused too,
// naming the version to install.
// Requirements: NFR-18
TEST(ApplicationTest, AnMsvcRuntimeWhoseVersionCannotBeReadIsRefusedWithOneLineNamingTheVersionToInstall) {
  const MsvcRuntimeFacts facts{.loaded = std::nullopt, .minimum = kMinimumMsvcRuntime};

  const auto checked = CheckMsvcRuntime(facts);

  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error(), MsvcRuntimeError::kUnreadable);
  EXPECT_EQ(DescribeMsvcRuntimeError("augustac", facts, checked.error()),
            "augustac: the version of the Visual C++ runtime it loaded (msvcp140.dll) cannot be read: install the "
            "Visual C++ Redistributable (x64) 14.44.35211.0 or newer, from https://aka.ms/vc14/vc_redist.x64.exe");
}

}  // namespace
