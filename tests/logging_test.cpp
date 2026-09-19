#include "augusta/logging.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <string_view>

namespace {

// 2024-02-01T12:00:00Z
constexpr std::chrono::seconds kFixedTime{1706788800};

// One formatted line, without its line ending (which is platform-specific).
std::string Format(spdlog::level::level_enum level, std::string_view message) {
  const spdlog::details::log_msg msg(spdlog::log_clock::time_point(kFixedTime), spdlog::source_loc{}, "augusta", level,
                                     message);
  spdlog::memory_buf_t buffer;
  augusta::logging::MakeFormatter()->format(msg, buffer);
  const std::string line(buffer.data(), buffer.size());
  return line.substr(0, line.find_first_of("\r\n"));
}

}  // namespace

TEST(LoggingFormat, LineIsUtcTimestampLevelThenMessage) {
  EXPECT_EQ(Format(spdlog::level::info, "subsystem=client event=starting"),
            "2024-02-01T12:00:00Z INFO subsystem=client event=starting");
}

TEST(LoggingFormat, LevelIsUpperCase) {
  EXPECT_EQ(Format(spdlog::level::trace, "m"), "2024-02-01T12:00:00Z TRACE m");
  EXPECT_EQ(Format(spdlog::level::debug, "m"), "2024-02-01T12:00:00Z DEBUG m");
  EXPECT_EQ(Format(spdlog::level::warn, "m"), "2024-02-01T12:00:00Z WARN m");
  EXPECT_EQ(Format(spdlog::level::err, "m"), "2024-02-01T12:00:00Z ERROR m");
  EXPECT_EQ(Format(spdlog::level::critical, "m"), "2024-02-01T12:00:00Z CRITICAL m");
}
