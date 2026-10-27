#include "augusta/logging.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <boost/log/attributes/attribute_name.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>

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

// The context every line carries (SetContext), under its lock. suffix is what
// its fields append to a message, kept ready so a line copies one string.
struct Context {
  std::mutex mutex;
  std::array<std::optional<std::string>, 3> fields;
  std::string suffix;
};

Context& GlobalContext() {
  static Context context;
  return context;
}

// The file sinks AddFileSink added, by their file, under their lock.
using FileSink = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;

struct FileSinks {
  std::mutex mutex;
  std::vector<std::pair<std::filesystem::path, boost::shared_ptr<FileSink>>> sinks;
};

FileSinks& GlobalFileSinks() {
  static FileSinks file_sinks;
  return file_sinks;
}

// The context's suffix again, for CopyContext to read without the lock: a
// sequence lock over atomics, so a read that a write overlaps sees the version
// change and is retried, and is never a data race. Constant-initialized, so
// reading it needs no guard of a function-local static. Written under
// Context's lock only.
struct PublishedContext {
  // Odd while a write is under way.
  std::atomic<std::uint32_t> version{0};
  std::atomic<std::size_t> size{0};
  std::array<std::atomic<char>, kContextCapacity> text{};
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<std::size_t>::is_always_lock_free &&
                  std::atomic<char>::is_always_lock_free,
              "a signal handler reads the published context");

constinit PublishedContext published_context;

// How many times CopyContext reads again when a write overlapped its read.
constexpr int kCopyAttempts = 4;

void Publish(std::string_view suffix) {
  const std::uint32_t version = published_context.version.load(std::memory_order_relaxed);
  published_context.version.store(version + 1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    published_context.text.at(i).store(suffix[i], std::memory_order_relaxed);
  }
  published_context.size.store(suffix.size(), std::memory_order_relaxed);
  published_context.version.store(version + 2, std::memory_order_release);
}

// The order the fields are appended in, and their keys.
constexpr std::array<std::pair<ContextField, std::string_view>, 3> kContextKeys{
    std::pair{ContextField::kRun, "run"},
    std::pair{ContextField::kSession, "session"},
    std::pair{ContextField::kMatch, "match"},
};

// Sets or clears field, and remakes the suffix and what CopyContext reads.
void ChangeContext(ContextField field, std::optional<std::string> value) {
  Context& context = GlobalContext();
  const std::scoped_lock lock(context.mutex);
  context.fields.at(static_cast<std::size_t>(field)) = std::move(value);
  context.suffix.clear();
  std::string published;
  for (const auto& [key_field, key] : kContextKeys) {
    if (const auto& set = context.fields.at(static_cast<std::size_t>(key_field))) {
      const std::string written = std::format(" {}={}", key, *set);
      context.suffix += written;
      if (published.size() + written.size() <= kContextCapacity) {
        published += written;
      }
    }
  }
  Publish(published);
}

std::string WithContext(std::string_view message) {
  Context& context = GlobalContext();
  const std::scoped_lock lock(context.mutex);
  std::string line;
  line.reserve(message.size() + context.suffix.size());
  line += message;
  line += context.suffix;
  return line;
}

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

std::string DescribeFileSinkError(const FileSinkError& error) {
  const std::string reason = error.reason ? ": " + error.reason.message() : "";
  switch (error.step) {
    case FileSinkError::Step::kCreateDirectory:
      return std::format("cannot create {}{}", Utf8(error.path), reason);
    case FileSinkError::Step::kOpen:
      return std::format("cannot open {} for appending{}", Utf8(error.path), reason);
  }
  return "?";
}

std::expected<void, FileSinkError> AddFileSink(const std::filesystem::path& file) {
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  if (error) {
    return std::unexpected(
        FileSinkError{.step = FileSinkError::Step::kCreateDirectory, .path = file.parent_path(), .reason = error});
  }
  // Binary, so a line ends in a bare line feed on every platform. The stream
  // says nothing of why it could not open; the C library under it sets errno.
  errno = 0;
  auto stream = boost::make_shared<std::ofstream>(file, std::ios::out | std::ios::app | std::ios::binary);
  if (!stream->is_open()) {
    return std::unexpected(FileSinkError{
        .step = FileSinkError::Step::kOpen, .path = file, .reason = std::error_code(errno, std::generic_category())});
  }
  const auto sink = boost::make_shared<FileSink>();
  sink->locked_backend()->add_stream(stream);
  // Flushed as written, as the console is: a crash must not eat the lines before it.
  sink->locked_backend()->auto_flush(true);
  sink->set_formatter([](const boost::log::record_view& record, boost::log::formatting_ostream& out) {
    FormatRecord(record, out, false);
  });
  boost::log::core::get()->add_sink(sink);
  FileSinks& file_sinks = GlobalFileSinks();
  const std::scoped_lock lock(file_sinks.mutex);
  file_sinks.sinks.emplace_back(file, sink);
  return {};
}

void RemoveFileSink(const std::filesystem::path& file) {
  FileSinks& file_sinks = GlobalFileSinks();
  const std::scoped_lock lock(file_sinks.mutex);
  // The last sink holding the file's stream, so erasing it closes the file.
  std::erase_if(file_sinks.sinks, [&file](const auto& added) {
    if (added.first != file) {
      return false;
    }
    boost::log::core::get()->remove_sink(added.second);
    return true;
  });
}

void SetContext(ContextField field, std::string_view value) { ChangeContext(field, std::string(value)); }

void ClearContext(ContextField field) { ChangeContext(field, std::nullopt); }

std::string_view CopyContext(ContextText& text) noexcept {
  for (int attempt = 0; attempt < kCopyAttempts; ++attempt) {
    const std::uint32_t before = published_context.version.load(std::memory_order_acquire);
    if (before % 2 != 0) {
      continue;
    }
    const std::size_t size = std::min(published_context.size.load(std::memory_order_relaxed), text.size());
    for (std::size_t i = 0; i < size; ++i) {
      text.at(i) = published_context.text.at(i).load(std::memory_order_relaxed);
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    if (published_context.version.load(std::memory_order_relaxed) == before) {
      return {text.data(), size};
    }
  }
  return {};
}

void Write(Severity level, std::string_view message) {
  static boost::log::sources::severity_logger_mt<Severity> logger;
  BOOST_LOG_SEV(logger, level) << boost::log::add_value(kTimeAttribute, std::chrono::system_clock::now())
                               << WithContext(message);
}

void SetLogLevel(Severity level) { runtime_floor.store(level, std::memory_order_relaxed); }

bool IsEnabled(Severity level) { return level >= runtime_floor.load(std::memory_order_relaxed); }

std::optional<Severity> ParseSeverity(std::string_view name) {
  static constexpr std::array<std::pair<std::string_view, Severity>, 6> kNames{
      std::pair{"trace", Severity::kTrace}, std::pair{"debug", Severity::kDebug},
      std::pair{"info", Severity::kInfo},   std::pair{"warn", Severity::kWarn},
      std::pair{"error", Severity::kError}, std::pair{"critical", Severity::kCritical},
  };
  // std::array's iterator is a raw pointer under libc++ (where clang-tidy wants
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
