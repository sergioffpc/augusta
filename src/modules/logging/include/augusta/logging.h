#ifndef AUGUSTA_LOGGING_H_
#define AUGUSTA_LOGGING_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

/// \file
/// augusta::logging writes the process's log lines (ADR-0027, ADR-0029, ADR-0036):
/// one process-wide, thread-safe console sink, a file sink beside it where the
/// process adds one, and a context of fields every line carries. Boost.Log does the
/// work behind logging.cpp; nothing here includes it, so no other translation
/// unit pays for its headers. A path a line, a failure's context or a message
/// names is written as its UTF-8 text (Utf8).
namespace augusta::logging {

/// path's text in UTF-8, whatever the system's code page: path::string()
/// converts to the ANSI code page on Windows and throws for a character it
/// lacks, so no source under src/ calls it (docs/agents/coding-standards.md).
/// Inline, so a module that only names paths links nothing of the logger.
[[nodiscard]] inline std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return {text.begin(), text.end()};
}

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
/// a second call does nothing. Installs the runtime floor below at kDebug (see
/// SetLogLevel): a Debug build is informed - the heartbeat and above - not
/// flooded by per-packet TRACE, unless a caller asks for it afterward.
void Init();

/// Why AddFileSink added no sink.
struct FileSinkError {
  enum class Step : std::uint8_t {
    /// The file's directory could not be made.
    kCreateDirectory,
    /// The file could not be opened for appending.
    kOpen,
  };
  Step step = Step::kOpen;
  /// The directory or the file the step failed on.
  std::filesystem::path path;
  /// The system's reason, where it gave one.
  std::error_code reason;
};

/// error in words, for a log line's `reason=`.
std::string DescribeFileSinkError(const FileSinkError& error);

/// Adds a second sink, the file at file, its directory created if missing:
/// every line written from now on reaches it as it reaches the console, without
/// colors, flushed as it is written (ADR-0027). The file is appended to. On
/// failure, why it could not be opened, and no sink is added. Thread-safe.
std::expected<void, FileSinkError> AddFileSink(const std::filesystem::path& file);

/// Removes the sink AddFileSink added for file, and closes the file: lines
/// written from now on no longer reach it. Nothing if file has no sink.
/// Thread-safe.
void RemoveFileSink(const std::filesystem::path& file);

/// The process-wide context every line carries (ADR-0029): each field set is
/// appended to every line's message, as `run=<value>`, `session=<value>` and
/// `match=<value>` in that order, so call sites never write them. Only a process
/// that holds one Run, Session and Match at a time sets them: the client.
enum class ContextField : std::uint8_t {
  kRun,
  kSession,
  kMatch,
};

/// Sets field to value for every line written from now on, from any thread.
void SetContext(ContextField field, std::string_view value);

/// Clears field: lines written from now on carry it no more.
void ClearContext(ContextField field);

/// Room for the context as CopyContext copies it: its three fields, each an ID
/// in its canonical form, with some to spare.
inline constexpr std::size_t kContextCapacity = 192;

/// What CopyContext copies the context into.
using ContextText = std::array<char, kContextCapacity>;

/// The context's fields as every line ends in them, ` run=<value>
/// session=<value>`, copied into text; empty if none is set. Takes no lock and
/// allocates nothing, so a signal handler or an unhandled-exception filter may
/// call it: the crash lines end in it (ADR-0047). A context changed in the
/// same instant, by another thread or by the code the handler interrupted, may
/// read as none, and a field that does not fit whole in kContextCapacity
/// after those before it is left out.
std::string_view CopyContext(ContextText& text) noexcept;

/// Writes message at level to the sinks, with the context's fields appended,
/// whatever the runtime floor. Use
/// the macros below instead: they drop calls under the compile-time level, and
/// under the runtime floor before formatting their message.
void Write(Severity level, std::string_view message);

/// Raises or lowers the runtime floor: below level, a call that still passes
/// the compile-time gate (AUGUSTA_LOG_ACTIVE_LEVEL) is now dropped before its
/// arguments are evaluated. This is what a config file's `logging.level` (ADR-0034)
/// drives - the compile-time gate alone can't tell a Debug build's TRACE
/// firehose apart from its DEBUG heartbeat, since both compile in together.
/// Thread-safe; takes effect for calls made after it returns. Until Init or a
/// first call, the floor is kTrace.
void SetLogLevel(Severity level);

/// Whether a call at level clears the runtime floor (SetLogLevel). One atomic
/// read, so the macros below can ask it on every call, hot paths included.
bool IsEnabled(Severity level);

/// Parses one of "trace", "debug", "info", "warn", "error", "critical"
/// (case-sensitive, matching the logfmt message bodies); anything else is
/// nullopt.
std::optional<Severity> ParseSeverity(std::string_view name);

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

// The runtime floor is checked first, so a call under it never runs std::format
// or evaluates its arguments: a per-packet TRACE line costs one atomic read.
#define AUGUSTA_LOG_AT(level, ...)                                                                     \
  (::augusta::logging::IsEnabled(level) ? ::augusta::logging::Write(level, ::std::format(__VA_ARGS__)) \
                                        : static_cast<void>(0))

// AUGUSTA_LOG_AT behind a logging::Throttle, for a line a peer can provoke as
// often as it likes, so it cannot flood the log: a line that follows suppressed
// ones ends in suppressed=<count>. Under the runtime floor it does not ask the
// throttle, so a filtered line takes no slot and counts as no suppressed line.
#define AUGUSTA_LOG_LIMITED_AT(level, throttle, ...)                                                                  \
  do {                                                                                                                \
    if (!::augusta::logging::IsEnabled(level)) {                                                                      \
      break;                                                                                                          \
    }                                                                                                                 \
    if (const auto augusta_suppressed = (throttle).Admit(::std::chrono::steady_clock::now())) {                       \
      ::augusta::logging::Write(level,                                                                                \
                                ::augusta::logging::WithSuppressed(::std::format(__VA_ARGS__), *augusta_suppressed)); \
    }                                                                                                                 \
  } while (false)

// LOG_-prefixed rather than bare level names: bare ones collide with macros and
// identifiers other headers define - <wingdi.h> (pulled in by <windows.h>, even
// under WIN32_LEAN_AND_MEAN) defines ERROR as 0, toolchains often pass -DDEBUG,
// and single letters like T clash with the T(...) functional-cast idiom GLM's
// templates use internally (glm/detail/_vectorize.hpp).
// The arguments are a std::format string and its values: LOG_INFO("event={}", 1).
// Each level's _LIMITED variant takes a logging::Throttle first:
// LOG_WARN_LIMITED(throttle, "event={}", 1) (see AUGUSTA_LOG_LIMITED_AT).
#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_TRACE
#define LOG_TRACE(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kTrace, __VA_ARGS__)
#define LOG_TRACE_LIMITED(throttle, ...) \
  AUGUSTA_LOG_LIMITED_AT(::augusta::logging::Severity::kTrace, throttle, __VA_ARGS__)
#else
#define LOG_TRACE(...) static_cast<void>(0)
#define LOG_TRACE_LIMITED(throttle, ...) static_cast<void>(0)
#endif

#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_DEBUG
#define LOG_DEBUG(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kDebug, __VA_ARGS__)
#define LOG_DEBUG_LIMITED(throttle, ...) \
  AUGUSTA_LOG_LIMITED_AT(::augusta::logging::Severity::kDebug, throttle, __VA_ARGS__)
#else
#define LOG_DEBUG(...) static_cast<void>(0)
#define LOG_DEBUG_LIMITED(throttle, ...) static_cast<void>(0)
#endif

#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_INFO
#define LOG_INFO(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kInfo, __VA_ARGS__)
#define LOG_INFO_LIMITED(throttle, ...) \
  AUGUSTA_LOG_LIMITED_AT(::augusta::logging::Severity::kInfo, throttle, __VA_ARGS__)
#else
#define LOG_INFO(...) static_cast<void>(0)
#define LOG_INFO_LIMITED(throttle, ...) static_cast<void>(0)
#endif

#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_WARN
#define LOG_WARN(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kWarn, __VA_ARGS__)
#define LOG_WARN_LIMITED(throttle, ...) \
  AUGUSTA_LOG_LIMITED_AT(::augusta::logging::Severity::kWarn, throttle, __VA_ARGS__)
#else
#define LOG_WARN(...) static_cast<void>(0)
#define LOG_WARN_LIMITED(throttle, ...) static_cast<void>(0)
#endif

#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_ERROR
#define LOG_ERROR(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kError, __VA_ARGS__)
#define LOG_ERROR_LIMITED(throttle, ...) \
  AUGUSTA_LOG_LIMITED_AT(::augusta::logging::Severity::kError, throttle, __VA_ARGS__)
#else
#define LOG_ERROR(...) static_cast<void>(0)
#define LOG_ERROR_LIMITED(throttle, ...) static_cast<void>(0)
#endif

#if AUGUSTA_LOG_ACTIVE_LEVEL <= AUGUSTA_LOG_LEVEL_CRITICAL
#define LOG_CRITICAL(...) AUGUSTA_LOG_AT(::augusta::logging::Severity::kCritical, __VA_ARGS__)
#define LOG_CRITICAL_LIMITED(throttle, ...) \
  AUGUSTA_LOG_LIMITED_AT(::augusta::logging::Severity::kCritical, throttle, __VA_ARGS__)
#else
#define LOG_CRITICAL(...) static_cast<void>(0)
#define LOG_CRITICAL_LIMITED(throttle, ...) static_cast<void>(0)
#endif

#endif  // AUGUSTA_LOGGING_H_
