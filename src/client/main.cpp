#include <expected>
#include <memory>
#include <optional>
#include <print>
#include <string_view>
#include <utility>

#include "application.h"
#include "augusta/application.h"
#include "augusta/assets.h"
#include "augusta/client_config.h"
#include "augusta/config.h"
#include "augusta/failure.h"
#include "augusta/logging.h"
#include "augusta/reenactment.h"
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

// The subsystem augustac's terminal event names (ADR-0029).
constexpr std::string_view kSubsystem = "client";

// The verified pack and the runtime that reads from it. The runtime holds on to
// the pack (it loads the characters other players bring from it), so the pack
// keeps its address and is declared first, to be destroyed last.
struct Client {
  std::unique_ptr<const augusta::assets::Pack> pack;
  std::unique_ptr<augusta::client::ClientRuntime> runtime;
};

// Loads what the runtime is made from and constructs it, as reenactment's
// Captured player if there is one (ADR-0050); the runtime's own exception (no
// window or GPU device, a rejected collision mesh) the application boundary
// classifies.
std::expected<std::unique_ptr<Client>, augusta::failure::Failure> ConstructClient(
    const augusta::config::ClientConfig& file_config, const std::optional<augusta::harness::Script>& reenactment) {
  auto loaded = augusta::client::LoadClient(file_config);
  if (!loaded) {
    return std::unexpected(std::move(loaded.error()));
  }
  if (reenactment.has_value()) {
    if (auto fits = augusta::client::CheckReenactmentPack(*reenactment, loaded->pack->Hash()); !fits) {
      return std::unexpected(std::move(fits.error()));
    }
  }

  augusta::client::RuntimeConfig config;
  config.renderer.title = "augusta";
  // Direct IP:port only, no server discovery (ARCHITECTURE.md §3).
  config.server.address = file_config.server_address;
  config.input = file_config.input;
  config.character = file_config.character;
  config.client_pack = loaded->pack->Hash();
  config.reenactment = reenactment;

  auto client = std::make_unique<Client>();
  client->pack = std::move(loaded->pack);
  client->runtime = std::make_unique<augusta::client::ClientRuntime>(config, std::move(loaded->content));
  return client;
}

// augustac's Lifecycle for file_config (augusta/application.h): the transport,
// then the client, then its run until the window closes, its reenactment ends
// or it fails - with no reconnecting and no connection screen, it says what
// happened and exits.
augusta::application::Lifecycle<Client> ClientLifecycle(const augusta::config::ClientConfig& file_config,
                                                        const std::optional<augusta::harness::Script>& reenactment) {
  return {
      .initialize = [] { return augusta::client::InitializeClientTransport(); },
      .construct = [&file_config, &reenactment] { return ConstructClient(file_config, reenactment); },
      .run = [](Client& client) { return client.runtime->Run(); },
  };
}

}  // namespace

int main(int argc, char** argv) {
  augusta::logging::Init();
#ifdef _WIN32
  const TimerResolution timer_resolution;
#endif

  // Settings come from a config file - augustac.yaml next to the executable
  // unless --config names another (ADR-0034) - not from the command line,
  // which otherwise only asks for --help or --version (printed, then exit), or
  // for this run to reenact a capture's player (ADR-0050).
  const auto command_line =
      augusta::config::ParseCommandLine(argc, argv, "augustac", augusta::config::kClientConfigFileName,
                                        augusta::EngineVersion(), augusta::config::kClientCommandLineOptions);
  if (command_line && command_line->action != augusta::config::CommandLineAction::kRun) {
    std::println("{}", command_line->message);
    return 0;
  }
  auto file_config = augusta::client::ReadClientConfig(command_line);
  if (!file_config) {
    return augusta::application::Conclude(kSubsystem, file_config.error());
  }
  // ParseClientConfig already validated log_level, so this is never nullopt.
  augusta::logging::SetLogLevel(*augusta::logging::ParseSeverity(file_config->log_level));
  LI("subsystem=client event=starting version={}", augusta::EngineVersion());

  // ReadClientConfig read the command line whole, so it has a value here.
  const auto reenactment = augusta::client::ReadReenactment(*command_line);
  if (!reenactment) {
    return augusta::application::Conclude(kSubsystem, reenactment.error());
  }
  // A Captured player plays its capture's Character, not the config's.
  if (reenactment->has_value()) {
    file_config->character = (*reenactment)->character;
  }

  return augusta::application::Conclude(kSubsystem,
                                        augusta::application::Execute(ClientLifecycle(*file_config, *reenactment)));
}
