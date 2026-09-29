#include "augusta/logging.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/log/attributes/attribute_name.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/log/utility/setup/console.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace augusta::logging {

namespace {

// The per-record attribute carrying when the message was written (the
// severity travels as Boost.Log's own "Severity" attribute).
constexpr const char* kTimeAttribute = "AugustaTime";

// The runtime floor (SetLogLevel). The macros read it on every call, so it is a
// lone atomic rather than a Boost.Log core filter: relaxed, since it guards no
// other data - a thread just sees a new floor a moment later.
std::atomic<Severity> runtime_floor{Severity::kTrace};

std::string_view LevelName(Severity level) {
  switch (level) {
    case Severity::kTrace:
      return "TRACE";
    case Severity::kDebug:
      return "DEBUG";
    case Severity::kInfo:
      return "INFO";
    case Severity::kWarn:
      return "WARN";
    case Severity::kError:
      return "ERROR";
    case Severity::kCritical:
      return "CRITICAL";
  }
  return "?";
}

std::string_view LevelColor(Severity level) {
  switch (level) {
    case Severity::kTrace:
      return "\x1b[37m";
    case Severity::kDebug:
      return "\x1b[36m";
    case Severity::kInfo:
      return "\x1b[32m";
    case Severity::kWarn:
      return "\x1b[33m\x1b[1m";
    case Severity::kError:
      return "\x1b[31m\x1b[1m";
    case Severity::kCritical:
      return "\x1b[1m\x1b[41m";
  }
  return "";
}

constexpr std::string_view kColorReset = "\x1b[m";

// True if stdout is a terminal that understands ANSI colors; on Windows this
// also switches the console to interpret them.
bool StdoutTakesColors() {
#ifdef _WIN32
  const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  // GetConsoleMode fails when stdout is redirected, which is the "no color" case.
  return handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) != 0 &&
         SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
  return isatty(STDOUT_FILENO) != 0;
#endif
}

void FormatRecord(const boost::log::record_view& record, boost::log::formatting_ostream& out, bool colored) {
  const auto time = boost::log::extract<std::chrono::system_clock::time_point>(kTimeAttribute, record);
  const auto level = boost::log::extract<Severity>("Severity", record);
  const auto message = boost::log::extract<std::string>("Message", record);
  if (!time || !level || !message) {
    return;
  }
  out << FormatLine(*time, *level, *message, colored);
}

}  // namespace

std::string FormatLine(std::chrono::system_clock::time_point time, Severity level, std::string_view message,
                       bool colored) {
  const auto seconds = std::chrono::floor<std::chrono::seconds>(time);
  if (!colored) {
    return std::format("{:%Y-%m-%dT%H:%M:%SZ} {} {}", seconds, LevelName(level), message);
  }
  return std::format("{:%Y-%m-%dT%H:%M:%SZ} {}{}{} {}", seconds, LevelColor(level), LevelName(level), kColorReset,
                     message);
}

void Init() {
  static std::once_flag once;
  std::call_once(once, [] {
    const bool colored = StdoutTakesColors();
    // Every record is flushed as it is written: stdout is captured by the
    // platform (ADR-0027), so a crash must not eat the lines before it.
    const auto sink = boost::log::add_console_log(std::cout, boost::log::keywords::auto_flush = true);
    sink->set_formatter([colored](const boost::log::record_view& record, boost::log::formatting_ostream& out) {
      FormatRecord(record, out, colored);
    });
    SetLogLevel(Severity::kDebug);
  });
}

void Write(Severity level, std::string_view message) {
  static boost::log::sources::severity_logger_mt<Severity> logger;
  BOOST_LOG_SEV(logger, level) << boost::log::add_value(kTimeAttribute, std::chrono::system_clock::now())
                               << std::string(message);
}

void SetLogLevel(Severity level) { runtime_floor.store(level, std::memory_order_relaxed); }

bool IsEnabled(Severity level) { return level >= runtime_floor.load(std::memory_order_relaxed); }

std::optional<Severity> ParseSeverity(std::string_view name) {
  static constexpr std::array<std::pair<std::string_view, Severity>, 6> kNames{
      std::pair{"trace", Severity::kTrace}, std::pair{"debug", Severity::kDebug},
      std::pair{"info", Severity::kInfo},   std::pair{"warn", Severity::kWarn},
      std::pair{"error", Severity::kError}, std::pair{"critical", Severity::kCritical},
  };
  // std::array's iterator is a raw pointer under libstdc++ (where clang-tidy wants
  // auto*) but MSVC's checked iterator is a class, so auto* fails to compile there.
  // NOLINTNEXTLINE(readability-qualified-auto)
  const auto found = std::ranges::find(kNames, name, &std::pair<std::string_view, Severity>::first);
  if (found == kNames.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<std::uint32_t> Throttle::Admit(std::chrono::steady_clock::time_point now) {
  if (last_admitted_.has_value() && now - *last_admitted_ < interval_) {
    ++suppressed_;
    return std::nullopt;
  }
  last_admitted_ = now;
  return std::exchange(suppressed_, 0U);
}

std::string WithSuppressed(std::string message, std::uint32_t count) {
  if (count > 0) {
    message += std::format(" suppressed={}", count);
  }
  return message;
}

}  // namespace augusta::logging
