#ifndef AUGUSTA_LOGGING_H_
#define AUGUSTA_LOGGING_H_

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <array>
#include <memory>
#include <string_view>

namespace augusta::logging {

namespace detail {

// spdlog's own `%l` prints the level in lower case, so the upper-case names
// the console format wants come from this custom flag (registered as `%*`).
class UpperCaseLevelFlag : public spdlog::custom_flag_formatter {
 public:
  void format(const spdlog::details::log_msg& msg, const std::tm& /*time*/, spdlog::memory_buf_t& dest) override {
    static constexpr std::array<std::string_view, spdlog::level::n_levels> kNames = {
        "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL", "OFF",
    };
    const std::string_view name = kNames[msg.level];
    dest.append(name.data(), name.data() + name.size());
  }

  [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
    return std::make_unique<UpperCaseLevelFlag>();
  }
};

}  // namespace detail

/// The console line format (ADR-0029): `<UTC ISO-8601 time> <LEVEL> <message>`,
/// e.g. `2024-02-01T12:00:00Z INFO subsystem=client event=starting`.
inline std::unique_ptr<spdlog::formatter> MakeFormatter() {
  auto formatter = std::make_unique<spdlog::pattern_formatter>(spdlog::pattern_time_type::utc);
  formatter->add_flag<detail::UpperCaseLevelFlag>('*').set_pattern("%Y-%m-%dT%H:%M:%SZ %^%*%$ %v");
  return formatter;
}

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
  logger->set_formatter(MakeFormatter());
  spdlog::set_default_logger(logger);
}

}  // namespace augusta::logging

// SPDLOG_ACTIVE_LEVEL (set per build type by the augusta_logging CMake
// target) decides at compile time which of these expand to real calls and
// which compile away entirely.
// L-prefixed rather than bare T/D/I/W/E/C: those collide with the T(...)
// functional-cast idiom GLM's templates use internally (glm/detail/_vectorize.hpp),
// which breaks compilation wherever a translation unit includes this header
// before something that pulls in <glm/...> (e.g. client/main.cpp -> runtime.h
// -> audio.h/physics.h -> math.h).
#define LT(...) SPDLOG_TRACE(__VA_ARGS__)
#define LD(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LI(...) SPDLOG_INFO(__VA_ARGS__)
#define LW(...) SPDLOG_WARN(__VA_ARGS__)
#define LE(...) SPDLOG_ERROR(__VA_ARGS__)
#define LC(...) SPDLOG_CRITICAL(__VA_ARGS__)

#endif  // AUGUSTA_LOGGING_H_
