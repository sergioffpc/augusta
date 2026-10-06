#include <chrono>
#include <filesystem>
#include <print>
#include <utility>

#include "augusta/assets.h"
#include "augusta/config.h"
#include "augusta/logging.h"
#include "augusta/map.h"
#include "augusta/networking.h"
#include "augusta/version.h"
#include "run.h"
#include "settings.h"

#ifdef _WIN32
#include <windows.h>
// timeapi.h needs the types windows.h declares, so it comes after it, unsorted.
#include <timeapi.h>
#endif

namespace {

// Verifies the pack the file's settings name, loads the Map from it and plays
// the run; returns the process's exit code: 0 only if the run succeeded.
int Run(const augusta::swarm::Settings& settings) {
  // Verified before any socket or thread is started, as augustac does (ADR-0018).
  const std::filesystem::path& pack_path = settings.pack_path;
  auto pack = augusta::assets::LoadVerifiedPack(pack_path, settings.public_key_path);
  if (!pack) {
    LE("subsystem=swarm event=pack_verification_failed path={} error={}", pack_path.string(),
       augusta::assets::DescribeVerifiedPackError(pack.error(), pack_path, settings.public_key_path));
    return 1;
  }
  auto map = augusta::map::LoadCollision(*pack);
  if (!map) {
    LE("subsystem=swarm event=map_loading_failed path={} error={}", pack_path.string(),
       augusta::map::DescribeMapError(map.error()));
    return 1;
  }

  const augusta::swarm::RunConfig config{
      .session = {.server = {.address = settings.server_address},
                  .client_pack = pack->Hash(),
                  .character = settings.character},
      .map = *std::move(map),
      .matches = settings.matches,
      .timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<float>(settings.timeout_seconds)),
      .seed = settings.seed,
  };
  return augusta::swarm::RunScriptedPlayers(config).verdict == augusta::swarm::Verdict::kSucceeded ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  augusta::logging::Init();
#ifdef _WIN32
  // As augustac does: Windows otherwise wakes a sleeping thread only every
  // 15.6 ms, too seldom for the Prediction threads to keep the tick rate.
  timeBeginPeriod(1);
#endif

  // Settings come from a config file - augusta-swarm.yaml next to the executable
  // unless --config names another (ADR-0034).
  const auto command_line = augusta::config::ParseCommandLine(
      argc, argv, "augusta-swarm", augusta::swarm::kSettingsFileName, augusta::EngineVersion());
  if (command_line && command_line->action != augusta::config::CommandLineAction::kRun) {
    std::println("{}", command_line->message);
    return 0;
  }
  const auto settings = command_line.and_then(
      [](const augusta::config::CommandLine& read) { return augusta::swarm::LoadSettings(read.config_file); });
  if (!settings) {
    LE("subsystem=swarm event=config_loading_failed error={}", augusta::swarm::DescribeSettingsError(settings.error()));
    return 1;
  }
  // ParseSettings already validated log_level, so this is never nullopt.
  augusta::logging::SetLogLevel(*augusta::logging::ParseSeverity(settings->log_level));
  LI("subsystem=swarm event=starting version={}", augusta::EngineVersion());

  // Once, process-wide, before any Session is constructed - see networking.h.
  augusta::networking::Init();
  return Run(*settings);
}
