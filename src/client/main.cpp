#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

#include "augusta/application.h"
#include "augusta/client_application.h"
#include "augusta/client_config.h"
#include "augusta/client_phases.h"
#include "augusta/client_run.h"
#include "augusta/config.h"
#include "augusta/logging.h"
#include "augusta/uuid.h"
#include "augusta/version.h"

namespace {

// The subsystem augustac's terminal event names (ADR-0029).
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
  // fails is named by its Run ID; a --help or --version is no Run.
  const augusta::uuid::Uuid run_id = augusta::client::BeginRun();
  const auto run = command_line.and_then(augusta::config::ReadClientRun);
  const auto file_config = augusta::client::ReadClientConfig(
      run.and_then([&command_line](const augusta::config::ClientRun& /*read*/) { return command_line; }));
  if (!file_config) {
    return augusta::application::Conclude(kSubsystem, file_config.error());
  }
  // ParseClientConfig already validated log_level, so this is never nullopt.
  augusta::logging::SetLogLevel(*augusta::logging::ParseSeverity(file_config->log_level));

  if (run->mode == augusta::config::ClientMode::kListReplays) {
    return augusta::application::Conclude(kSubsystem, augusta::client::ListReplays(*file_config));
  }
  std::optional<std::string> replay =
      run->mode == augusta::config::ClientMode::kWatchReplay ? std::optional(run->capture) : std::nullopt;

  // ReadClientConfig read the command line whole, so it has a value here.
  auto reenactment = augusta::client::ReadReenactment(*command_line);
  if (!reenactment) {
    return augusta::application::Conclude(kSubsystem, reenactment.error());
  }

  return augusta::application::Conclude(
      kSubsystem, augusta::application::Execute(augusta::client::ClientLifecycle(*file_config, *std::move(reenactment),
                                                                                 std::move(replay), run_id)));
}
