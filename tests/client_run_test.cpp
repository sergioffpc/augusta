#include "augusta/client_run.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <gtest/gtest.h>

#include "augusta/crash.h"
#include "augusta/gpu_crash_dump_files.h"
#include "augusta/logging.h"
#include "augusta/run_files.h"

// What a launch tells the player of the Run before it (ADR-0027): a crashed
// one, in one WARN line naming it and its files, and nothing otherwise. And
// the client's Session in its log (ADR-0029): from its admission to its end,
// every line carries `session=`, and none does after.
namespace {

using augusta::client::GpuDumpPath;
using augusta::client::LogPath;
using augusta::client::MatchLogContext;
using augusta::client::MinidumpPath;
using augusta::client::ReportPreviousRun;
using augusta::client::SessionLogContext;
using Uuid = boost::uuids::uuid;

Uuid RunId(std::uint8_t n) {
  return boost::uuids::uuid(
      {0x01, 0x9a, 0x07, 0x5c, 0x4f, 0x10, 0x7a, 0x01, 0x8b, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, n});
}

std::size_t Occurrences(std::string_view text, std::string_view needle) {
  std::size_t count = 0;
  for (std::size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

class ClientRunTest : public testing::Test {
 protected:
  void SetUp() override {
    const testing::TestInfo* info = testing::UnitTest::GetInstance()->current_test_info();
    directory_ = std::filesystem::temp_directory_path() / (std::string("augusta-client-run-") + info->name());
    std::filesystem::remove_all(directory_);
    augusta::logging::Init();
    augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& Directory() const { return directory_; }

  static void Write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
  }

  // What ReportPreviousRun writes for the Run current.
  [[nodiscard]] std::string Reported(const Uuid& current) const {
    testing::internal::CaptureStdout();
    ReportPreviousRun(directory_, current);
    return testing::internal::GetCapturedStdout();
  }

 private:
  std::filesystem::path directory_;
};

TEST_F(ClientRunTest, ALaunchAfterACrashedRunWritesOneWarningNamingItAndItsFiles) {
  Write(LogPath(Directory(), RunId(1)), "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crash signal=SIGSEGV\n");
  Write(LogPath(Directory(), RunId(2)), "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crash signal=SIGSEGV\n");
  Write(MinidumpPath(Directory(), RunId(2)), "dump");

  const std::string written = Reported(RunId(3));

  EXPECT_EQ(Occurrences(written, "\n"), 1U) << written;
  const std::u8string directory = Directory().u8string();
  const std::string id = boost::uuids::to_string(RunId(2));
  EXPECT_EQ(Occurrences(written, "WARN subsystem=client event=previous_run_crashed crashed_run=" + id +
                                     " directory=\"" + std::string(directory.begin(), directory.end()) +
                                     "\" files=\"dumps/augustac-" + id + ".dmp logs/augustac-" + id + ".log\""),
            1U)
      << written;
}

// Run 2 follows a crashed Run, so it is told of it: the warning is written in
// this setup. Run 3 follows Run 2's terminal failure, and Run 1 follows none.
TEST_F(ClientRunTest, ALaunchAfterAnyOtherEndWritesNothing) {
  Write(LogPath(Directory(), RunId(1)), "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crash signal=SIGSEGV\n");
  Write(LogPath(Directory(), RunId(2)),
        "2026-10-21T10:00:01Z CRITICAL subsystem=client event=terminal_failure code=server_unreachable\n");

  EXPECT_NE(Reported(RunId(2)), "");
  EXPECT_EQ(Reported(RunId(3)), "");
  EXPECT_EQ(Reported(RunId(1)), "");
}

// A Run taken and begun, as augustac's main takes and begins one either side
// of installing the crash handler.
augusta::client::Run StartRun(const std::optional<std::filesystem::path>& directory) {
  return augusta::client::BeginRun(augusta::client::TakeRun(directory));
}

// What StartRun left behind, undone: its log file's sink and its Run in the
// context, so the directory can go and no later test's line carries the Run.
void EndRun(const augusta::client::Run& run) {
  if (run.log_file) {
    augusta::logging::RemoveFileSink(*run.log_file);
  }
  augusta::logging::ClearContext(augusta::logging::ContextField::kRun);
}

// Taking a Run names its files but makes none, so the crash handler is
// installed before the Run touches the disk.
TEST_F(ClientRunTest, TakingARunNamesItsFilesAndMakesNone) {
  const augusta::client::Run run = augusta::client::TakeRun(Directory());
  augusta::logging::ClearContext(augusta::logging::ContextField::kRun);

  EXPECT_EQ(run.directory, Directory());
  EXPECT_EQ(run.log_file, LogPath(Directory(), run.id));
  EXPECT_FALSE(std::filesystem::exists(Directory()));
}

TEST_F(ClientRunTest, ARunsLogFileAndDumpsAreUnderItsDirectoryNamedByItsId) {
  const augusta::client::Run run = StartRun(Directory());
  EndRun(run);

  EXPECT_EQ(run.log_file, LogPath(Directory(), run.id));
  EXPECT_TRUE(std::filesystem::exists(LogPath(Directory(), run.id)));
#ifdef _WIN32
  EXPECT_EQ(run.minidump_file, MinidumpPath(Directory(), run.id));
#else
  EXPECT_FALSE(run.minidump_file.has_value());
#endif
}

// Whether text is a UUID's canonical form: 36 characters, lowercase hex
// digits, hyphens where RFC 9562 puts them.
bool IsCanonical(std::string_view text) {
  if (text.size() != 36) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    const bool hyphen = i == 8 || i == 13 || i == 18 || i == 23;
    const bool digit = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (hyphen ? c != '-' : !digit) {
      return false;
    }
  }
  return true;
}

// The Run ID is a UUIDv7 (GLOSSARY.md): one taken after another is later, so
// the Runs' files sort in the order the Runs began.
TEST_F(ClientRunTest, TwoRunIdsTakenOneAfterTheOtherDifferSortInOrderAndAreCanonicalVersion7) {
  const augusta::client::Run first = StartRun(std::nullopt);
  EndRun(first);
  const augusta::client::Run second = StartRun(std::nullopt);
  EndRun(second);

  const std::string first_text = boost::uuids::to_string(first.id);
  const std::string second_text = boost::uuids::to_string(second.id);
  EXPECT_NE(first.id, second.id);
  EXPECT_LT(first.id, second.id);
  EXPECT_LT(first_text, second_text);
  EXPECT_TRUE(IsCanonical(first_text)) << first_text;
  EXPECT_EQ(first_text[14], '7') << first_text;
}

TEST_F(ClientRunTest, ARunWithNoDirectoryLogsOnlyToTheConsoleAndSaysSo) {
  testing::internal::CaptureStdout();
  const augusta::client::Run run = StartRun(std::nullopt);
  const std::string written = testing::internal::GetCapturedStdout();
  EndRun(run);

  EXPECT_FALSE(run.log_file.has_value());
  EXPECT_FALSE(run.minidump_file.has_value());
  EXPECT_NE(written.find("WARN subsystem=client event=log_file_unavailable"), std::string::npos) << written;
  // Its GPUs, or that it found none, whatever the machine running the test.
  EXPECT_NE(written.find("subsystem=client event=gpu"), std::string::npos) << written;
}

TEST_F(ClientRunTest, ARunNamesItsGpuCrashDumpsAfterItsId) {
  const augusta::client::Run run = StartRun(Directory());
  EndRun(run);

  const auto written = augusta::renderer::WriteGpuCrashDump(std::as_bytes(std::span("gpu")));

  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(*written, GpuDumpPath(Directory(), run.id, 1));
}

// A launch tells of the crashed Run before it, then keeps only the newest
// Runs, its own among them.
TEST_F(ClientRunTest, ALaunchReportsTheCrashedRunBeforeItThenKeepsOnlyTheNewestRuns) {
  const auto runs = static_cast<std::uint8_t>(augusta::client::kKeptRuns + 1);
  for (std::uint8_t n = 1; n <= runs; ++n) {
    Write(LogPath(Directory(), RunId(n)),
          "2026-10-21T10:00:01Z CRITICAL subsystem=client event=crash signal=SIGSEGV\n");
  }

  testing::internal::CaptureStdout();
  ReportPreviousRun(Directory(), RunId(runs));
  augusta::client::PruneOldRuns(Directory());
  const std::string written = testing::internal::GetCapturedStdout();

  EXPECT_NE(written.find("event=previous_run_crashed crashed_run=" +
                         boost::uuids::to_string(RunId(static_cast<std::uint8_t>(runs - 1)))),
            std::string::npos)
      << written;
  EXPECT_FALSE(std::filesystem::exists(LogPath(Directory(), RunId(1))));
  EXPECT_TRUE(std::filesystem::exists(LogPath(Directory(), RunId(2))));
}

// A crash names its Run and writes into the Run's log; then on Windows its
// minidump, and on Linux where its core is.
TEST_F(ClientRunTest, TheCrashHandlerIsGivenTheRunAndWhereItsCrashGoes) {
  const augusta::client::Run run{.id = RunId(1),
                                 .log_file = LogPath(Directory(), RunId(1)),
                                 .minidump_file = MinidumpPath(Directory(), RunId(1)),
                                 .directory = Directory()};

  const augusta::crash::Settings settings = augusta::client::CrashSettings(run);

  EXPECT_EQ(settings.run_id, run.id);
  EXPECT_EQ(settings.log_file, run.log_file);
  EXPECT_EQ(settings.minidump_file, run.minidump_file);
#ifdef _WIN32
  EXPECT_EQ(settings.core_location, std::nullopt);
#else
  EXPECT_EQ(settings.core_location, "coredumpctl dump augustac");
#endif
}

// A Run with no files - its directory unknown - gives the handler none.
TEST_F(ClientRunTest, ARunWithNoFilesGivesTheCrashHandlerNone) {
  const augusta::client::Run run{
      .id = RunId(1), .log_file = std::nullopt, .minidump_file = std::nullopt, .directory = std::nullopt};

  const augusta::crash::Settings settings = augusta::client::CrashSettings(run);

  EXPECT_EQ(settings.run_id, run.id);
  EXPECT_EQ(settings.log_file, std::nullopt);
  EXPECT_EQ(settings.minidump_file, std::nullopt);
}

const Uuid kSession = boost::uuids::uuid(
    {0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD4, 0x70, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11});

// What one LOG_INFO of message writes to the console.
std::string WrittenInfoLine(std::string_view message) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  testing::internal::CaptureStdout();
  LOG_INFO("{}", message);
  return testing::internal::GetCapturedStdout();
}

TEST(SessionLogContextTest, EveryLineWhileTheSessionIsHeldCarriesItsSessionId) {
  const SessionLogContext held(kSession);

  const std::string written = WrittenInfoLine("subsystem=test event=admitted");

  EXPECT_NE(written.find("event=admitted session=0190a1b2-c3d4-7000-8000-000000000011\n"), std::string::npos)
      << written;
}

// While held, a line carries the Session; once it ends, none does.
TEST(SessionLogContextTest, NoLineAfterTheSessionEndsCarriesASessionId) {
  std::string during;
  {
    const SessionLogContext held(kSession);
    during = WrittenInfoLine("subsystem=test event=playing");
  }

  const std::string written = WrittenInfoLine("subsystem=test event=ended");

  EXPECT_NE(during.find("session="), std::string::npos) << during;
  EXPECT_EQ(written.find("session="), std::string::npos) << written;
}

const Uuid kMatch = boost::uuids::uuid(
    {0x01, 0x90, 0xA1, 0xB2, 0xC3, 0xD4, 0x70, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22});

TEST(MatchLogContextTest, EveryLineFromAMatchStartToItsEndCarriesItsMatchIdAndNoneAfter) {
  MatchLogContext held;
  held.Hold(kMatch);
  const std::string during = WrittenInfoLine("subsystem=test event=playing");
  held.Hold(Uuid{});
  const std::string after = WrittenInfoLine("subsystem=test event=in_lobby");

  EXPECT_NE(during.find("event=playing match=0190a1b2-c3d4-7000-8000-000000000022\n"), std::string::npos) << during;
  EXPECT_EQ(after.find("match="), std::string::npos) << after;
}

// Held when it goes, the Match is carried no more.
TEST(MatchLogContextTest, NoLineAfterItGoesCarriesAMatchId) {
  std::string during;
  {
    MatchLogContext held;
    held.Hold(kMatch);
    during = WrittenInfoLine("subsystem=test event=playing");
  }

  const std::string written = WrittenInfoLine("subsystem=test event=closed");

  EXPECT_NE(during.find("match="), std::string::npos) << during;
  EXPECT_EQ(written.find("match="), std::string::npos) << written;
}

}  // namespace
