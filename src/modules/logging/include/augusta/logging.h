#ifndef AUGUSTA_LOGGING_H_
#define AUGUSTA_LOGGING_H_

#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

// augusta::logging writes the process's log lines (ADR-0027, ADR-0029, ADR-0036):
// one process-wide, thread-safe console sink, no file sink. Boost.Log does the
// work behind logging.cpp; nothing here includes it, so no other translation
// unit pays for its headers.
namespace augusta::logging {

/// A log line's severity, lowest to highest.
enum class Severity : std::uint8_t {
  kTrace,
  kDebug,
  kInfo,
  kWarn,
  kError,
  kCritical,
};

/// One console line (ADR-0029): `<UTC ISO-8601 time> <LEVEL> <message>`, e.g.
/// `2024-02-01T12:00:00Z INFO subsystem=client event=starting`, without its line
/// ending. With colored, the level name is wrapped in ANSI color codes.
std::string FormatLine(std::chrono::system_clock::time_point time, Severity level, std::string_view message,
                       bool colored);

/// Configures the process-wide console sink. Colors are used only when stdout
/// is a terminal. Call once at process startup, before using the macros below;
/// a second call does nothing.
void Init();

/// Writes message at level to the console sink. Use the macros below instead:
/// they also drop calls under the compile-time level.
void Write(Severity level, std::string_view message);

/// Lets one event through per interval and counts those it turned away, so a line
/// that can repeat every tick or every packet is written once an interval with
/// how many it stood for. Takes the time as an argument, so it needs no clock to
/// test. Not thread-safe: use one from one thread, or under the caller's lock.
class Throttle {
 public:
  explicit Throttle(std::chrono::steady_clock::duration interval) : interval_(interval) {}

  /// Whether the event at now is let through: if so, how many were turned away
  /// since the last one that was. The first event is always let through.
  std::optional<std::uint32_t> Admit(std::chrono::steady_clock::time_point now);

 private:
  std::chrono::steady_clock::duration interval_;
  std::optional<std::chrono::steady_clock::time_point> last_admitted_;
  std::uint32_t suppressed_ = 0;
};

/// message with ` suppressed=<count>` appended when count is not zero.
std::string WithSuppressed(std::string message, std::uint32_t count);

}  // namespace augusta::logging

// AUGUSTA_LOG_ACTIVE_LEVEL (set per build type by the augusta_logging CMake
// target) decides at compile time which of the macros below expand to real
// calls and which compile away entirely: Debug keeps everything from trace
// up, other builds strip everything under info.
#define AUGUSTA_LOG_LEVEL_TRACE 0
#define AUGUSTA_LOG_LEVEL_DEBUG 1
#define AUGUSTA_LOG_LEVEL_INFO 2
#define AUGUSTA_LOG_LEVEL_WARN 3
#define AUGUSTA_LOG_LEVEL_ERROR 4
#define AUGUSTA_LOG_LEVEL_CRITICAL 5

#ifndef AUGUSTA_LOG_ACTIVE_LEVEL
#define AUGUSTA_LOG_ACTIVE_LEVEL AUGUSTA_LOG_LEVEL_INFO
#endif

#define AUGUSTA_LOG_AT(level, ...) ::augusta::logging::Write(level, ::std::format(__VA_ARGS__))

// L-prefixed rather than bare T/D/I/W/E/C: those collide with the T(...)
// functional-cast idiom GLM's templates use internally (glm/detail/_vectorize.hpp),
// which breaks compilation wherever a translation unit includes this header
// before something that pulls in <glm/...> (e.g. client/main.cpp -> runtime.h
// -> audio.h/physics.h -> math.h).
// The arguments are a std::format string and its values: LI("event={}", 1).
#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_TRACE
#define LT(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kTrace, __VA_ARGS__)
#else
#define LT(...) static_cast<void>(0)
#endif

#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_DEBUG
#define LD(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kDebug, __VA_ARGS__)
#else
#define LD(...) static_cast<void>(0)
#endif

#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_INFO
#define LI(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kInfo, __VA_ARGS__)
#else
#define LI(...) static_cast<void>(0)
#endif

#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_WARN
#define LW(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kWarn, __VA_ARGS__)
#else
#define LW(...) static_cast<void>(0)
#endif

// LW_LIMITED(throttle, ...) is LW behind a logging::Throttle: for a warning a peer
// can provoke as often as it likes, so it cannot flood the log. A line that
// follows suppressed ones ends in suppressed=<count>.
#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_WARN
#define LW_LIMITED(throttle, ...)                                                                                     \
  do {                                                                                                                \
    if (const auto augusta_suppressed = (throttle).Admit(::std::chrono::steady_clock::now())) {                       \
      ::augusta::logging::Write(::augusta::logging::Severity::kWarn,                                                  \
                                ::augusta::logging::WithSuppressed(::std::format(__VA_ARGS__), *augusta_suppressed)); \
    }                                                                                                                 \
  } while (false)
#else
#define LW_LIMITED(throttle, ...) static_cast<void>(0)
#endif

#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_ERROR
#define LE(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kError, __VA_ARGS__)
#else
#define LE(...) static_cast<void>(0)
#endif

#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_CRITICAL
#define LC(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kCritical, __VA_ARGS__)
#else
#define LC(...) static_cast<void>(0)
#endif

#endif  // AUGUSTA_LOGGING_H_
