#ifndef AUGUSTA_LOGGING_H_
#define AUGUSTA_LOGGING_H_

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace augusta::logging {

// Configures the process-wide default logger: a single thread-safe, colored
// console sink (ADR-0027 — no file sink, the platform captures stdout).
// Flushes immediately on warnings and above; lower levels stay buffered.
// Call once at process startup, before using the macros below.
inline void Init() {
  // Idempotent: a second call (e.g. from a future test binary linking this
  // module) reuses the already-registered logger instead of crashing —
  // spdlog::stdout_color_mt() throws if "augusta" is already registered.
  auto logger = spdlog::get("augusta");
  if (!logger) {
    logger = spdlog::stdout_color_mt("augusta");
  }
  // Keep the runtime filter in lockstep with the compile-time one: a call
  // below SPDLOG_ACTIVE_LEVEL doesn't exist in the binary, but one at or
  // above it still needs the logger's own level raised to let it through
  // (spdlog defaults new loggers to `info`).
  logger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
  logger->flush_on(spdlog::level::warn);
  spdlog::set_default_logger(logger);
}

}  // namespace augusta::logging

// SPDLOG_ACTIVE_LEVEL (set per build type by the augusta_logging CMake
// target) decides at compile time which of these expand to real calls and
// which compile away entirely.
#define TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define ERR(...) SPDLOG_ERROR(__VA_ARGS__)
#define CRIT(...) SPDLOG_CRITICAL(__VA_ARGS__)

#endif  // AUGUSTA_LOGGING_H_
