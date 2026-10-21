#include "augusta/client_run.h"

#include <filesystem>
#include <format>
#include <string>

#include "augusta/config.h"
#include "augusta/hardware.h"
#include "augusta/logging.h"
#include "augusta/uuid.h"
#include "augusta/version.h"

namespace augusta::client {

namespace {

// path in UTF-8, which, unlike path.string() on Windows, never throws for a
// character the code page lacks.
std::string Utf8(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return {utf8.begin(), utf8.end()};
}

// Adds the Run's log file as a second sink, or warns why it could not.
void StartLogFile(const uuid::Uuid& run_id) {
  const auto directory = config::ExecutableDirectory();
  if (!directory) {
    LOG_WARN("subsystem=client event=log_file_unavailable reason=\"executable directory unknown\"");
    return;
  }
  const std::filesystem::path file = *directory / "logs" / std::format("augustac-{}.log", run_id);
  if (auto added = logging::AddFileSink(file); !added) {
    LOG_WARN("subsystem=client event=log_file_unavailable path=\"{}\" reason=\"{}\"", Utf8(file), added.error());
  }
}

}  // namespace

uuid::Uuid BeginRun() {
  const uuid::Uuid run_id = uuid::Generate();
  logging::SetContext(logging::ContextField::kRun, run_id.ToString());
  StartLogFile(run_id);
  LOG_INFO("subsystem=client event=starting version={} {}", EngineVersion(), DescribeBuild());
  LOG_INFO("subsystem=client event=hardware {}", hardware::DescribeMachine(hardware::QueryMachine()));
  return run_id;
}

}  // namespace augusta::client
