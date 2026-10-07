#include <csignal>
#include <filesystem>
#include <memory>
#include <print>
#include <utility>

#include "augusta/assets.h"
#include "augusta/config.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/version.h"
#include "content.h"
#include "crash.h"
#include "host.h"
#include "runtime.h"

namespace {

// Signal handlers can't capture context, so this is the only way to
// reach the one ServerRuntime main() constructs - set just before
// Run() is called, never reassigned afterward.
augusta::server::ServerRuntime* g_runtime = nullptr;

extern "C" void HandleShutdownSignal(int /*signal*/) {
  if (g_runtime != nullptr) {
    g_runtime->Stop();
  }
}

// Verifies the pack the file's settings name, loads its content and creates the
// runtime from both; or logs why it could not and returns nullptr. Nothing in
// the content refers back to the pack, so it goes once the content is loaded.
std::unique_ptr<augusta::server::ServerRuntime> CreateRuntime(const augusta::config::ServerConfig& file_config) {
  // Verified before anything else starts (no socket, world, or thread is spun
  // up yet) - a bad pack or key means this process exits here, never partially
  // running against untrusted content (ADR-0018, ARCHITECTURE.md §8).
  const std::filesystem::path& pack_path = file_config.pack_path;
  const auto pack = augusta::assets::LoadVerifiedPack(pack_path, file_config.public_key_path);
  if (!pack) {
    LE("subsystem=server event=pack_verification_failed path={} error={}", pack_path.string(),
       augusta::assets::DescribeVerifiedPackError(pack.error(), pack_path, file_config.public_key_path));
    return nullptr;
  }
  LI("subsystem=server event=pack_verified path={}", pack_path.string());

  auto content = augusta::server::LoadServerContent(*pack, file_config.tick_rate_hz);
  if (!content) {
    LE("subsystem=server event=content_loading_failed path={} error={}", pack_path.string(),
       augusta::server::DescribeContentError(content.error()));
    return nullptr;
  }

  const augusta::server::HostConfig config{
      .tick_rate_hz = file_config.tick_rate_hz,
      // Every client is sent the rate and these when it joins and predicts with
      // them, so the config file and the scenario's script are the only places
      // they are set.
      .parameters = content->parameters,
      .listen = {.address = file_config.listen_address},
      .recording = file_config.recording_path,
      .server_pack = pack->Hash(),
  };
  if (!config.recording.empty()) {
    LI("subsystem=server event=recording path={}", config.recording.string());
  }
  return std::make_unique<augusta::server::ServerRuntime>(config, file_config.metrics_port,
                                                          std::move(content->scenario), std::move(content->policy));
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
  const auto file_config = command_line.and_then(
      [](const augusta::config::CommandLine& read) { return augusta::config::LoadServerConfig(read.config_file); });
  if (!file_config) {
    LE("subsystem=server event=config_loading_failed error={}",
       augusta::config::DescribeConfigError(file_config.error()));
    return 1;
  }
  // ParseServerConfig already validated log_level, so this is never nullopt.
  augusta::logging::SetLogLevel(*augusta::logging::ParseSeverity(file_config->log_level));
  LI("subsystem=server event=starting version={}", augusta::EngineVersion());

  // augusta::networking::Init() must run once, process-wide, before any
  // Client/Server is constructed - see networking.h.
  augusta::networking::Init();

  const auto runtime = CreateRuntime(*file_config);
  if (!runtime) {
    return 1;
  }
  g_runtime = runtime.get();
  std::signal(SIGINT, HandleShutdownSignal);
  std::signal(SIGTERM, HandleShutdownSignal);

  // A failure was logged by the supervisor where it happened; it only sets the exit status here.
  return runtime->Run().has_value() ? 1 : 0;
}
