#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

#include "augusta/application.h"
#include "augusta/client_config.h"
#include "augusta/config.h"
#include "augusta/crash.h"
#include "augusta/logging.h"
#include "augusta/version.h"
#include "client_application.h"
#include "client_phases.h"
#include "client_run.h"

namespace {

// The subsystem augustac's terminal event and crash lines name (ADR-0029).
constexpr std::string_view kSubsystem = "client";

}  // namespace

int main(int argc, char** argv) {
  augusta::logging::Init();

  // Settings come from a config file - augustac.yaml next to the executable
  // unless --config names another (ADR-0034) - not from the command line,
  // which otherwise only asks for --help or --version (printed, then exit),
  // and what one run is for: a Replay to list or watch (ADR-0051), or a
  // capture's player to reenact (ADR-0050).
  const auto command_line =
      augusta::config::ParseCommandLine(argc, argv, "augustac", augusta::config::kClientConfigFileName,
                                        augusta::EngineVersion(), augusta::config::kClientOptions);
  if (command_line && command_line->action != augusta::config::CommandLineAction::kRun) {
    std::println("{}", command_line->message);
    return 0;
  }
  // The Run starts here, before its config is read, so even a config that
  // fails is named by its Run ID; a --help or --version is no Run, so only
  // reading the command line comes first. Its files are beside the executable.
  // The crash handler is installed as soon as there is a Run to name, before
  // the Run opens, reads or deletes any file. Its terminal event names its log
  // file, for the player to send.
  const std::optional<std::filesystem::path> run_directory = augusta::config::ExecutableDirectory();
  const augusta::client::Run taken_run = augusta::client::TakeRun(run_directory);
  augusta::crash::Install(argv[0], kSubsystem, augusta::client::CrashSettings(taken_run));
  const augusta::client::Run client_run = augusta::client::BeginRun(taken_run);
  if (run_directory) {
    augusta::client::ReportPreviousRun(*run_directory, client_run.id);
    augusta::client::PruneOldRuns(*run_directory);
  }
  const auto conclude = [&client_run](const augusta::application::Outcome& outcome) {
    return augusta::application::Conclude(kSubsystem, outcome, client_run.log_file);
  };
  const auto run = command_line.and_then(augusta::config::ReadClientRun);
  const auto file_config = augusta::client::ReadClientConfig(
      run.and_then([&command_line](const augusta::config::ClientRun& /*read*/) { return command_line; }));
  if (!file_config) {
    return conclude(file_config.error());
  }
  // ParseClientConfig already validated log_level, so this is never nullopt.
  augusta::logging::SetLogLevel(*augusta::logging::ParseSeverity(file_config->log_level));

  if (run->mode == augusta::config::ClientMode::kListReplays) {
    return conclude(augusta::client::ListReplays(*file_config));
  }
  std::optional<std::string> replay =
      run->mode == augusta::config::ClientMode::kWatchReplay ? std::optional(run->capture) : std::nullopt;

  // ReadClientConfig read the command line whole, so it has a value here.
  auto reenactment = augusta::client::ReadReenactment(*command_line);
  if (!reenactment) {
    return conclude(reenactment.error());
  }

  return conclude(augusta::application::Execute(
      augusta::client::ClientProgram(*file_config, *std::move(reenactment), std::move(replay), client_run.id)));
}
