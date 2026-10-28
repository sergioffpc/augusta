#include <cstdio>  // IWYU pragma: keep - stderr, which include-cleaner traces to glibc's <stdio.h>
#include <expected>
#include <optional>
#include <print>
#include <string_view>

#if AUGUSTA_MIMALLOC
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
#include "server_application.h"
#include "shutdown_signal.h"

namespace {

// The subsystem augustad's terminal event and crash lines name (ADR-0029).
constexpr std::string_view kSubsystem = "server";

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

// Whether augustad may run on what serves its malloc (ADR-0078): mimalloc's
// facts once application's check says mimalloc serves the process, else why
// not, which main words on stderr as there is no log yet; nullopt in a
// sanitizer build, which runs its sanitizer's allocator and checks nothing.
// main asks first, before its command line, so --version fails too.
std::expected<std::optional<augusta::application::MimallocFacts>, augusta::application::MimallocError>
CheckAllocator() {
#if AUGUSTA_MIMALLOC
  const augusta::application::MimallocFacts facts = ReadMimallocFacts();
  return augusta::application::CheckMimalloc(facts).transform([&facts] { return std::optional(facts); });
#else
  return std::nullopt;
#endif
}

}  // namespace

int main(int argc, char** argv) {
  const auto mimalloc = CheckAllocator();
  if (!mimalloc) {
    std::println(stderr, "{}", augusta::application::DescribeMimallocError("augustad", mimalloc.error()));
    return augusta::application::kFailedExitStatus;
  }

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

  // Before the Program, so a signal during startup stops the run as it starts.
  augusta::server::InstallShutdownSignalHandlers();
  const auto program = augusta::server::ServerProgram(*file_config, *mimalloc);
  // The runtime's supervisor logged a runtime failure where it happened; this
  // is the process's one terminal event for it.
  return augusta::application::Conclude(kSubsystem, augusta::application::Execute(program));
}
