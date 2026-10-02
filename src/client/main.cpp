#include <filesystem>
#include <optional>
#include <print>
#include <utility>

#include "augusta/assets.h"
#include "augusta/config.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/version.h"
#include "runtime.h"

#ifdef _WIN32
#include <windows.h>
// timeapi.h needs the types windows.h declares, so it comes after it, unsorted.
#include <timeapi.h>
#endif

namespace {

#ifdef _WIN32
// Raises the system timer resolution to 1 ms for as long as it lives, and
// restores it after. Windows otherwise wakes a sleeping thread only every
// 15.6 ms, so the Prediction thread could not keep a 60 Hz schedule (16.7 ms
// a tick) and would run well below the server's rate.
class TimerResolution {
 public:
  TimerResolution() : raised_(timeBeginPeriod(kPeriodMs) == TIMERR_NOERROR) {
    if (!raised_) {
      LW("subsystem=client event=timer_resolution_unchanged period_ms={}", kPeriodMs);
    }
  }
  ~TimerResolution() {
    if (raised_) {
      timeEndPeriod(kPeriodMs);
    }
  }

  TimerResolution(const TimerResolution&) = delete;
  TimerResolution& operator=(const TimerResolution&) = delete;
  TimerResolution(TimerResolution&&) = delete;
  TimerResolution& operator=(TimerResolution&&) = delete;

 private:
  static constexpr UINT kPeriodMs = 1;
  bool raised_;
};
#endif

augusta::runtime::Config BuildRuntimeConfig(const augusta::config::ClientConfig& file_config,
                                            const augusta::assets::Pack& pack) {
  augusta::runtime::Config config;
  config.renderer.title = "augusta";
  // Direct IP:port only, no server discovery (ARCHITECTURE.md §3).
  config.server.address = file_config.server_address;
  config.input = file_config.input;
  config.character = file_config.character;
  config.client_pack = pack.Hash();
  return config;
}

std::optional<augusta::runtime::Failure> Run(const augusta::runtime::Config& config,
                                             augusta::runtime::Content content) {
  // augusta::networking::Init() must run once, process-wide, before any
  // Client/Server is constructed - see networking.h.
  augusta::networking::Init();

  augusta::runtime::ClientRuntime runtime(config, std::move(content));
  return runtime.Run();
}

}  // namespace

int main(int argc, char** argv) {
  augusta::logging::Init();
#ifdef _WIN32
  const TimerResolution timer_resolution;
#endif

  // Settings come from a config file - augustac.yaml next to the executable
  // unless --config names another (ADR-0034) - not from the command line,
  // which otherwise only asks for --help or --version (printed, then exit).
  const auto command_line = augusta::config::ParseCommandLine(
      argc, argv, "augustac", augusta::config::kClientConfigFileName, augusta::EngineVersion());
  if (command_line && command_line->action != augusta::config::CommandLineAction::kRun) {
    std::println("{}", command_line->message);
    return 0;
  }
  const auto file_config = command_line.and_then(
      [](const augusta::config::CommandLine& read) { return augusta::config::LoadClientConfig(read.config_file); });
  if (!file_config) {
    LE("subsystem=client event=config_loading_failed error={}",
       augusta::config::DescribeConfigError(file_config.error()));
    return 1;
  }
  // ParseClientConfig already validated log_level, so this is never nullopt.
  augusta::logging::SetLogLevel(*augusta::logging::ParseSeverity(file_config->log_level));
  LI("subsystem=client event=starting version={}", augusta::EngineVersion());

  // Verified before anything else starts (no renderer/audio device,
  // network socket, or thread is spun up yet) - a bad pack or key means
  // this process exits here, never partially running against untrusted
  // content (ADR-0018, ARCHITECTURE.md §8).
  const std::filesystem::path& pack_path = file_config->pack_path;
  const auto pack = augusta::assets::LoadVerifiedPack(pack_path, file_config->public_key_path);
  if (!pack) {
    LE("subsystem=client event=pack_verification_failed path={} error={}", pack_path.string(),
       augusta::assets::DescribeVerifiedPackError(pack.error(), pack_path, file_config->public_key_path));
    return 1;
  }
  LI("subsystem=client event=pack_verified path={}", pack->Path().string());

  auto content = augusta::runtime::LoadClientContent(*pack, file_config->character);
  if (!content) {
    LE("subsystem=client event=content_loading_failed path={} error={}", pack->Path().string(),
       augusta::runtime::DescribeContentError(content.error()));
    return 1;
  }

  const augusta::runtime::Config config = BuildRuntimeConfig(*file_config, *pack);
  if (const auto failure = Run(config, *std::move(content)); failure.has_value()) {
    // No reconnecting and no connection screen: say what happened and exit.
    LE("subsystem=client event=run_failed path={} error={}", pack->Path().string(),
       augusta::runtime::DescribeFailure(*failure));
    return 1;
  }

  return 0;
}
