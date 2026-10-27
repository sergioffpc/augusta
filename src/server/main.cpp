#include <atomic>
#include <csignal>
#include <optional>
#include <print>
#include <string_view>
#include <utility>

#if AUGUSTA_MIMALLOC
#include <cstdio>
#include <cstdlib>

#include <mimalloc.h>
#ifdef _WIN32
// operator new and delete as mimalloc's own, in this one file of the
// executable, as mimalloc asks of a program its DLL redirects malloc for. Its
// static library, on Linux and macOS, defines them itself.
#include <mimalloc-new-delete.h>  // IWYU pragma: keep
#endif
#endif

#include "augusta/application.h"
#include "augusta/config.h"
#include "augusta/crash.h"
#include "augusta/logging.h"
#include "augusta/server_config.h"
#include "augusta/version.h"
#include "server.h"
#include "server_application.h"

namespace {

// The subsystem augustad's terminal event and crash lines name (ADR-0029).
constexpr std::string_view kSubsystem = "server";

// Signal handlers can't capture context, so this is the only way to
// reach the one ServerRuntime main() constructs - set just before Run() is
// called, and cleared once it returns or throws, before the runtime is released.
std::atomic<augusta::server::ServerRuntime*> g_runtime{nullptr};
static_assert(std::atomic<augusta::server::ServerRuntime*>::is_always_lock_free,
              "the signal handler must read it lock-free");

extern "C" void HandleShutdownSignal(int /*signal*/) {
  if (augusta::server::ServerRuntime* runtime = g_runtime.load(); runtime != nullptr) {
    runtime->Stop();
  }
}

// Points the signal handlers at runtime for as long as it lives, and away
// from it again however run ends - an exception included, which the
// application boundary catches before it releases the runtime.
class SignalTarget {
 public:
  explicit SignalTarget(augusta::server::ServerRuntime& runtime) {
    g_runtime = &runtime;
    std::signal(SIGINT, HandleShutdownSignal);
    std::signal(SIGTERM, HandleShutdownSignal);
  }
  ~SignalTarget() { g_runtime = nullptr; }

  SignalTarget(const SignalTarget&) = delete;
  SignalTarget& operator=(const SignalTarget&) = delete;
  SignalTarget(SignalTarget&&) = delete;
  SignalTarget& operator=(SignalTarget&&) = delete;
};

#if AUGUSTA_MIMALLOC
// What mimalloc says of this process (ADR-0078): whether it redirected the
// CRT's malloc (Windows; the link replaced it elsewhere), whether a block malloc
// hands out now is in its heap, and its version.
augusta::application::MimallocFacts ReadMimallocFacts() {
  void* const block = std::malloc(1);  // NOLINT(cppcoreguidelines-no-malloc): malloc itself is what is checked
  const augusta::application::MimallocFacts facts{
#ifdef _WIN32
      .redirected = mi_is_redirected(),
#else
      .redirected = true,
#endif
      .block_in_heap = mi_is_in_heap_region(block),
      .version = mi_version(),
  };
  std::free(block);  // NOLINT(cppcoreguidelines-no-malloc)
  return facts;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  // Before anything else, the command line included, so --version fails too:
  // augustad refuses to run on any allocator but mimalloc, saying why in one
  // line, as there is no log yet (ADR-0078). A sanitizer build runs its
  // sanitizer's allocator, and checks nothing.
  std::optional<augusta::application::MimallocFacts> mimalloc;
#if AUGUSTA_MIMALLOC
  mimalloc = ReadMimallocFacts();
  if (const auto served = augusta::application::CheckMimalloc("augustad", *mimalloc); !served) {
    std::println(stderr, "{}", served.error());
    return augusta::application::kFailedExitStatus;
  }
#endif

  // First of the rest, so a crash anywhere after it - startup included - is
  // logged and leaves a core dump (ADR-0047).
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

  auto program = augusta::server::ServerProgram(*file_config, mimalloc);
  program.run = [run = std::move(program.run)](augusta::server::ServerRuntime& runtime) {
    const SignalTarget target(runtime);
    return run(runtime);
  };
  // The runtime's supervisor logged a runtime failure where it happened; this
  // is the process's one terminal event for it.
  return augusta::application::Conclude(kSubsystem, augusta::application::Execute(program));
}
