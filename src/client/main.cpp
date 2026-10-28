#include <cstdio>
#include <expected>
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#ifdef _WIN32
#include <cstddef>
#include <vector>

#include <windows.h>
#endif

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

#ifdef _WIN32
// The MSVC runtime this build loads: the debug one in a Debug build (/MDd).
#ifdef _DEBUG
constexpr const wchar_t* kMsvcRuntimeDll = L"msvcp140d.dll";
#else
constexpr const wchar_t* kMsvcRuntimeDll = L"msvcp140.dll";
#endif

// The longest path GetModuleFileNameW writes, in characters with its null.
constexpr DWORD kMaxModulePath = 32768;

// The file version of the DLL the process loaded as dll, from its version
// resource; nullopt when it is not loaded or has no readable version.
std::optional<augusta::application::FileVersion> ReadLoadedFileVersion(const wchar_t* dll) {
  const HMODULE module = GetModuleHandleW(dll);
  if (module == nullptr) {
    return std::nullopt;
  }
  std::wstring path(kMaxModulePath, L'\0');
  const DWORD length = GetModuleFileNameW(module, path.data(), kMaxModulePath);
  if (length == 0 || length == kMaxModulePath) {
    return std::nullopt;
  }
  path.resize(length);
  const DWORD size = GetFileVersionInfoSizeW(path.c_str(), nullptr);
  if (size == 0) {
    return std::nullopt;
  }
  std::vector<std::byte> info(size);
  if (GetFileVersionInfoW(path.c_str(), 0, size, info.data()) == FALSE) {
    return std::nullopt;
  }
  VS_FIXEDFILEINFO* fixed = nullptr;
  UINT fixed_size = 0;
  if (VerQueryValueW(info.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixed_size) == FALSE ||
      fixed_size < sizeof(VS_FIXEDFILEINFO)) {
    return std::nullopt;
  }
  return augusta::application::FileVersion{
      .major = HIWORD(fixed->dwFileVersionMS),
      .minor = LOWORD(fixed->dwFileVersionMS),
      .build = HIWORD(fixed->dwFileVersionLS),
      .revision = LOWORD(fixed->dwFileVersionLS),
  };
}

// What augustac knows of the MSVC runtime it loaded (ADR-0079): its
// msvcp140.dll's version, and the oldest it runs on, as versions.env pins it
// (src/client/CMakeLists.txt).
augusta::application::MsvcRuntimeFacts ReadMsvcRuntimeFacts() {
  return {.loaded = ReadLoadedFileVersion(kMsvcRuntimeDll), .minimum = AUGUSTA_MSVC_REDIST_VERSION};
}
#endif

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

// Whether augustac may run on what serves its malloc (ADR-0078): mimalloc's
// facts once application's check says mimalloc serves the process, else why
// not; nullopt in a sanitizer build, which runs its sanitizer's allocator and
// checks nothing.
std::expected<std::optional<augusta::application::MimallocFacts>, augusta::application::MimallocError>
CheckAllocator() {
#if AUGUSTA_MIMALLOC
  const augusta::application::MimallocFacts facts = ReadMimallocFacts();
  return augusta::application::CheckMimalloc(facts).transform([&facts] { return std::optional(facts); });
#else
  return std::nullopt;
#endif
}

// Why the MSVC runtime refused augustac, with the facts its line names.
struct MsvcRuntimeRefusal {
  augusta::application::MsvcRuntimeFacts facts;
  augusta::application::MsvcRuntimeError error;
};

// Why augustac may not start: its MSVC runtime (Windows), or mimalloc.
using StartupError = std::variant<MsvcRuntimeRefusal, augusta::application::MimallocError>;

// Whether augustac may start: on Windows, first, whether the player's Visual
// C++ runtime is as new as the STL it was built with (ADR-0079), as with an
// older one it would crash later, with no word of why; then CheckAllocator's
// answer. main asks first, before its command line, so --version fails too.
std::expected<std::optional<augusta::application::MimallocFacts>, StartupError> CheckStartup() {
#ifdef _WIN32
  const augusta::application::MsvcRuntimeFacts runtime = ReadMsvcRuntimeFacts();
  if (const auto checked = augusta::application::CheckMsvcRuntime(runtime); !checked) {
    return std::unexpected(MsvcRuntimeRefusal{.facts = runtime, .error = checked.error()});
  }
#endif
  return CheckAllocator().transform_error(
      [](augusta::application::MimallocError error) { return StartupError(error); });
}

// The one line main writes on stderr for error, as there is no log yet.
std::string DescribeStartupError(const StartupError& error) {
  if (const auto* refusal = std::get_if<MsvcRuntimeRefusal>(&error)) {
    return augusta::application::DescribeMsvcRuntimeError("augustac", refusal->facts, refusal->error);
  }
  return augusta::application::DescribeMimallocError("augustac", std::get<augusta::application::MimallocError>(error));
}

}  // namespace

int main(int argc, char** argv) {
  const auto mimalloc = CheckStartup();
  if (!mimalloc) {
    std::println(stderr, "{}", DescribeStartupError(mimalloc.error()));
    return augusta::application::kFailedExitStatus;
  }
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
  const augusta::client::Run client_run = augusta::client::BeginRun(taken_run, *mimalloc);
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
