#include <print>
#include <string_view>

#include "augusta/application.h"
#include "augusta/config.h"
#include "augusta/crash.h"
#include "augusta/logging.h"
#include "augusta/server_config.h"
#include "augusta/version.h"
#include "server_application.h"
#include "shutdown_signal.h"

namespace {

// The subsystem augustad's terminal event and crash lines name (ADR-0029).
constexpr std::string_view kSubsystem = "server";

}  // namespace

int main(int argc, char** argv) {
  // First, so a crash anywhere after it - startup included - is logged and
  // leaves a core dump (ADR-0047).
  augusta::crash::Install(argv[0], kSubsystem);
  augusta::logging::Init();

  // Settings come from a config file - augustad.yaml next to the executable
  // unless --config names another (ADR-0034) - not from the command line,
  // which otherwise only asks for --help or --version (printed, then exit).
  const auto command_line = augusta::config::ParseCommandLine(
      argc, argv, "augustad", augusta::config::kServerConfigFileName, augusta::EngineVersion());
  if (command_line && command_line->action != augusta::config::CommandLineAction::kRun) {
    std::println("{}", command_line->message);
    return 0;
  }
  const auto file_config = augusta::server::ReadServerConfig(command_line);
  if (!file_config) {
    return augusta::application::Conclude(kSubsystem, file_config.error());
  }
  // ParseServerConfig already validated log_level, so this is never nullopt.
  augusta::logging::SetLogLevel(*augusta::logging::ParseSeverity(file_config->log_level));
  LOG_INFO("subsystem=server event=starting version={}", augusta::EngineVersion());

  // Before the Program, so a signal during startup stops the run as it starts.
  augusta::server::InstallShutdownSignalHandlers();
  const auto program = augusta::server::ServerProgram(*file_config);
  // The runtime's supervisor logged a runtime failure where it happened; this
  // is the process's one terminal event for it.
  return augusta::application::Conclude(kSubsystem, augusta::application::Execute(program));
}
