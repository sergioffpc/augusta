#include <atomic>
#include <csignal>
#include <print>
#include <string_view>
#include <utility>

#include "application.h"
#include "augusta/application.h"
#include "augusta/config.h"
#include "augusta/logging.h"
#include "augusta/server_config.h"
#include "augusta/version.h"
#include "crash.h"
#include "runtime.h"

namespace {

// The subsystem augustad's terminal event names (ADR-0029).
constexpr std::string_view kSubsystem = "server";

// Signal handlers can't capture context, so this is the only way to
// reach the one ServerRuntime main() constructs - set just before Run() is
// called, and cleared once it returns, before the runtime is released.
std::atomic<augusta::server::ServerRuntime*> g_runtime{nullptr};
static_assert(std::atomic<augusta::server::ServerRuntime*>::is_always_lock_free,
              "the signal handler must read it lock-free");

extern "C" void HandleShutdownSignal(int /*signal*/) {
  if (augusta::server::ServerRuntime* runtime = g_runtime.load(); runtime != nullptr) {
    runtime->Stop();
  }
}

}  // namespace

int main(int argc, char** argv) {
  // First, so a crash anywhere after it - startup included - is logged and
  // leaves a core dump (ADR-0047).
  augusta::server::InstallCrashHandler(argv[0]);
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
  LI("subsystem=server event=starting version={}", augusta::EngineVersion());

  auto lifecycle = augusta::server::ServerLifecycle(*file_config);
  lifecycle.run = [run = std::move(lifecycle.run)](augusta::server::ServerRuntime& runtime) {
    g_runtime = &runtime;
    std::signal(SIGINT, HandleShutdownSignal);
    std::signal(SIGTERM, HandleShutdownSignal);
    auto outcome = run(runtime);
    g_runtime = nullptr;
    return outcome;
  };
  // The runtime's supervisor logged a runtime failure where it happened; this
  // is the process's one terminal event for it.
  return augusta::application::Conclude(kSubsystem, augusta::application::Execute(lifecycle));
}
