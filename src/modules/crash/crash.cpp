#include "augusta/crash.h"

#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include "absl/base/no_destructor.h"
#include "augusta/logging.h"
#include "crash_platform.h"

namespace augusta::crash {

namespace {

// Everything from here to Install runs inside a handler, so it allocates
// nothing, takes no lock and calls only async-signal-safe functions.

// What Install was given, set before any handler is: the subsystem a literal,
// the core location a view into GlobalSettings, empty where not given.
std::string_view g_subsystem;
std::string_view g_core_location;

// The settings Install was given, never destroyed, so a crash during exit still
// reads them.
Settings& GlobalSettings() {
  static absl::NoDestructor<Settings> settings;
  return *settings;
}

constexpr int kMaxFrames = 64;

constexpr std::int64_t kSecondsPerMinute = 60;
constexpr std::int64_t kSecondsPerHour = 60 * kSecondsPerMinute;
constexpr std::int64_t kSecondsPerDay = 24 * kSecondsPerHour;

constexpr std::uint64_t kDecimal = 10;
constexpr std::uint64_t kHexadecimal = 16;

// Appends to a Line, always keeping room for the newline Finish ends it
// with, so a line too long is cut rather than overrun.
class LineWriter {
 public:
  explicit LineWriter(Line& line) : line_(line) {}

  // Whether n characters still fit with reserve kept after them.
  [[nodiscard]] bool Fits(std::size_t n, std::size_t reserve) const { return size_ + n + reserve <= line_.size(); }

  void Put(char c, std::size_t reserve = 1) {
    if (Fits(1, reserve)) {
      line_[size_++] = c;
    }
  }

  void Put(std::string_view text) {
    for (const char c : text) {
      Put(c);
    }
  }

  // value in base (up to 16), zero-padded to width digits.
  void PutDigits(std::uint64_t value, std::uint64_t base, int width = 1) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    // Enough for any std::uint64_t in base 2 or more.
    std::array<char, std::numeric_limits<std::uint64_t>::digits> digits{};
    int count = 0;
    do {
      digits[count++] = kDigits[value % base];
      value /= base;
    } while (value != 0 || count < width);
    while (count > 0) {
      Put(digits[--count]);
    }
  }

  // text in double quotes, its quotes and backslashes escaped. Cut short if the
  // line runs out, keeping room for after more characters after it, but always
  // closed.
  void PutQuoted(std::string_view text, std::size_t after = 0) {
    // The closing quote, what follows it and the newline.
    const std::size_t closing = 2 + after;
    Put('"');
    for (const char c : text) {
      const bool escaped = c == '"' || c == '\\';
      if (!Fits(escaped ? 2 : 1, closing)) {
        break;
      }
      if (escaped) {
        Put('\\', closing);
      }
      Put(c, closing);
    }
    Put('"');
  }

  std::string_view Finish() {
    line_[size_++] = '\n';
    return {line_.data(), size_};
  }

 private:
  Line& line_;
  std::size_t size_ = 0;
};

// `YYYY-MM-DDTHH:MM:SSZ`, as logging::FormatLine writes it, from time
// through the proleptic Gregorian calendar (H. Hinnant's civil_from_days):
// std::chrono's calendar types would do it too, but std::format allocates.
void PutTimestamp(LineWriter& writer, std::chrono::sys_seconds time) {
  const std::int64_t unix_seconds = time.time_since_epoch().count();
  const std::int64_t days = unix_seconds / kSecondsPerDay;
  const std::int64_t second_of_day = unix_seconds % kSecondsPerDay;
  const std::int64_t shifted = days + 719468;  // Days from 0000-03-01.
  const std::int64_t era = shifted / 146097;
  const std::int64_t day_of_era = shifted - (era * 146097);
  const std::int64_t year_of_era =
      (day_of_era - (day_of_era / 1460) + (day_of_era / 36524) - (day_of_era / 146096)) / 365;
  const std::int64_t day_of_year = day_of_era - ((365 * year_of_era) + (year_of_era / 4) - (year_of_era / 100));
  const std::int64_t shifted_month = ((5 * day_of_year) + 2) / 153;  // March is 0.
  const std::int64_t day = day_of_year - (((153 * shifted_month) + 2) / 5) + 1;
  const std::int64_t month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
  const std::int64_t year = year_of_era + (era * 400) + (month <= 2 ? 1 : 0);

  writer.PutDigits(static_cast<std::uint64_t>(year), kDecimal, 4);
  writer.Put('-');
  writer.PutDigits(static_cast<std::uint64_t>(month), kDecimal, 2);
  writer.Put('-');
  writer.PutDigits(static_cast<std::uint64_t>(day), kDecimal, 2);
  writer.Put('T');
  writer.PutDigits(static_cast<std::uint64_t>(second_of_day / kSecondsPerHour), kDecimal, 2);
  writer.Put(':');
  writer.PutDigits(static_cast<std::uint64_t>((second_of_day % kSecondsPerHour) / kSecondsPerMinute), kDecimal, 2);
  writer.Put(':');
  writer.PutDigits(static_cast<std::uint64_t>(second_of_day % kSecondsPerMinute), kDecimal, 2);
  writer.Put('Z');
}

void PutCrashPrefix(LineWriter& writer, std::string_view subsystem, std::chrono::sys_seconds time) {
  PutTimestamp(writer, time);
  writer.Put(" CRITICAL subsystem=");
  writer.Put(subsystem);
  writer.Put(' ');
}

// What every frame line starts with: `... event=crash_frame index=<index>
// pc=0x<pc>`.
void PutFramePrefix(LineWriter& writer, std::string_view subsystem, std::chrono::sys_seconds time, int index,
                    std::uintptr_t pc) {
  PutCrashPrefix(writer, subsystem, time);
  writer.Put("event=");
  writer.Put(kCrashFrameEvent);
  writer.Put(" index=");
  writer.PutDigits(static_cast<std::uint64_t>(index), kDecimal);
  writer.Put(" pc=0x");
  writer.PutDigits(pc, kHexadecimal);
}

// How many digits PutDigits writes for value in hexadecimal.
std::size_t HexLength(std::uint64_t value) {
  std::size_t length = 1;
  while (value >= kHexadecimal) {
    value /= kHexadecimal;
    ++length;
  }
  return length;
}

std::string_view SignalName(int signal) {
  switch (signal) {
    case SIGSEGV:
      return "SIGSEGV";
    case SIGILL:
      return "SIGILL";
    case SIGFPE:
      return "SIGFPE";
    case SIGABRT:
      return "SIGABRT";
#ifdef SIGBUS
    case SIGBUS:
      return "SIGBUS";
#endif
    default:
      return {};
  }
}

// time() rather than system_clock::now(): POSIX lists it as async-signal-safe.
std::chrono::sys_seconds Now() { return std::chrono::sys_seconds{std::chrono::seconds{std::time(nullptr)}}; }

// Every crash's lines, the log's context at the crash ending each: the first,
// which first(line, now, context) writes into line, then one per frame of the
// stack capture(frames) stores, innermost first, returning how many. A lambda,
// not std::function, which may allocate.
template <typename FirstLine>
void WriteCrashLines(FirstLine first, FrameCapture capture) {
  const std::chrono::sys_seconds now = Now();
  logging::ContextText context_text{};
  const std::string_view context = logging::CopyContext(context_text);
  Line line{};
  WriteLine(first(line, now, context));
  std::array<void*, kMaxFrames> frames{};
  const int depth = capture(std::span<void*>(frames));
  WriteFrameLines(std::span<void* const>(frames.data(), static_cast<std::size_t>(depth)), g_subsystem, now, context,
                  line);
}

}  // namespace

void WriteSignalLines(int signal, FrameCapture capture) {
  WriteCrashLines(
      [signal](Line& line, std::chrono::sys_seconds now, std::string_view context) {
        return FormatSignalLine(line, g_subsystem, now, signal, g_core_location, context);
      },
      capture);
}

void WriteExceptionLines(std::uint32_t code, FrameCapture capture) {
  WriteCrashLines(
      [code](Line& line, std::chrono::sys_seconds now, std::string_view context) {
        return FormatExceptionLine(line, g_subsystem, now, code, context);
      },
      capture);
}

std::string_view FormatSignalLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int signal,
                                  std::string_view core, std::string_view context) {
  LineWriter writer(line);
  PutCrashPrefix(writer, subsystem, time);
  writer.Put("event=");
  writer.Put(kCrashEvent);
  writer.Put(" signal=");
  if (const std::string_view name = SignalName(signal); !name.empty()) {
    writer.Put(name);
  } else {
    writer.PutDigits(static_cast<std::uint64_t>(signal), kDecimal);
  }
  if (!core.empty()) {
    writer.Put(" core=");
    writer.PutQuoted(core, context.size());
  }
  writer.Put(context);
  return writer.Finish();
}

std::string_view FormatFrameLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int index,
                                 std::uintptr_t pc, const char* symbol, std::string_view context) {
  LineWriter writer(line);
  PutFramePrefix(writer, subsystem, time, index, pc);
  if (symbol != nullptr) {
    writer.Put(" symbol=");
    // The context kept whole: a line that loses it no longer says its Run.
    writer.PutQuoted(symbol, context.size());
  }
  writer.Put(context);
  return writer.Finish();
}

std::string_view FormatModuleFrameLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time, int index,
                                       std::uintptr_t pc, const char* module, std::uintptr_t offset,
                                       std::string_view context) {
  LineWriter writer(line);
  PutFramePrefix(writer, subsystem, time, index, pc);
  if (module != nullptr) {
    // The offset and the context kept whole: a frame without its offset names
    // no instruction.
    constexpr std::string_view kOffsetKey = " offset=0x";
    writer.Put(" module=");
    writer.PutQuoted(module, kOffsetKey.size() + HexLength(offset) + context.size());
    writer.Put(kOffsetKey);
    writer.PutDigits(offset, kHexadecimal);
  }
  writer.Put(context);
  return writer.Finish();
}

std::string_view FormatExceptionLine(Line& line, std::string_view subsystem, std::chrono::sys_seconds time,
                                     std::uint32_t code, std::string_view context) {
  constexpr int kCodeDigits = 8;
  LineWriter writer(line);
  PutCrashPrefix(writer, subsystem, time);
  writer.Put("event=");
  writer.Put(kCrashEvent);
  writer.Put(" exception=0x");
  writer.PutDigits(code, kHexadecimal, kCodeDigits);
  writer.Put(context);
  return writer.Finish();
}

void Install(const char* argv0, std::string_view subsystem, Settings settings) {
  g_subsystem = subsystem;
  Settings& kept = GlobalSettings();
  kept = std::move(settings);
  if (kept.core_location) {
    g_core_location = *kept.core_location;
  }
  if (kept.log_file) {
    // Its directory made first, as the log sink would: Install comes before
    // it. If it cannot be, the file is not opened.
    std::error_code ignored;
    std::filesystem::create_directories(kept.log_file->parent_path(), ignored);
    OpenLogFile(*kept.log_file);
  }
  InstallHandlers(argv0, kept);
}

}  // namespace augusta::crash
