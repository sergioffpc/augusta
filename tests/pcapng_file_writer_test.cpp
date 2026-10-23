#include "augusta/pcapng_file_writer.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/faults.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/pcapng.h"

// The pcapng file writer (ADR-0076, Development build only), in a directory of
// the test's temp folder: what it writes, read back block by block, and how it
// stops.
namespace augusta::pcapng {
namespace {

using failure::Faults;
using failure::Site;

// 2023-11-14T22:13:20.123456Z.
constexpr std::chrono::system_clock::time_point kStarted{std::chrono::microseconds(1'700'000'000'123'456)};
constexpr std::string_view kExecutable = "augustac";
constexpr std::string_view kVersion = "1.2.3+dev";

std::filesystem::path FreshDirectory(std::string_view name) {
  const std::filesystem::path directory = std::filesystem::path(testing::TempDir()) / "augusta_pcapng_test" / name;
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  return directory;
}

std::vector<std::byte> ReadWhole(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  const auto bytes = std::as_bytes(std::span(text));
  return {bytes.begin(), bytes.end()};
}

std::uint32_t ReadU32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
  }
  return value;
}

// A second reader: file's blocks, each whole, by the total length each states
// at its start and repeats at its end; nullopt if one is cut short or its two
// lengths disagree.
std::optional<std::vector<std::vector<std::byte>>> ReadBlocks(const std::filesystem::path& file) {
  const std::vector<std::byte> bytes = ReadWhole(file);
  std::vector<std::vector<std::byte>> blocks;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    if (bytes.size() - offset < 12) {
      return std::nullopt;
    }
    const std::uint32_t length = ReadU32(bytes, offset + 4);
    if (length < 12 || length % 4 != 0 || bytes.size() - offset < length ||
        ReadU32(bytes, offset + length - 4) != length) {
      return std::nullopt;
    }
    blocks.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
  }
  return blocks;
}

std::vector<std::byte> PayloadOf(std::string_view text) {
  const auto bytes = std::as_bytes(std::span(text));
  return {bytes.begin(), bytes.end()};
}

networking::TappedMessage MessageOf(std::span<const std::byte> payload, networking::TapDirection direction) {
  return {
      .direction = direction,
      .local = {.address = networking::Ipv4Address{127, 0, 0, 1}, .port = 27015},
      .remote = {.address = networking::Ipv4Address{127, 0, 0, 1}, .port = 50000},
      .payload = payload,
      .time = kStarted + std::chrono::milliseconds(5),
  };
}

// Every log line written while it lives, through a file sink of its own.
class LogLines {
 public:
  explicit LogLines(std::string_view name)
      : file_(std::filesystem::path(testing::TempDir()) / "augusta_pcapng_test" / "logs" / name) {
    std::filesystem::remove(file_);
    logging::Init();
    EXPECT_TRUE(logging::AddFileSink(file_).has_value());
  }
  ~LogLines() { logging::RemoveFileSink(file_); }
  LogLines(const LogLines&) = delete;
  LogLines& operator=(const LogLines&) = delete;
  LogLines(LogLines&&) = delete;
  LogLines& operator=(LogLines&&) = delete;

  // The lines that hold text.
  [[nodiscard]] std::vector<std::string> Holding(std::string_view text) const {
    std::ifstream in(file_);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
      if (line.find(text) != std::string::npos) {
        lines.push_back(line);
      }
    }
    return lines;
  }

 private:
  std::filesystem::path file_;
};

TEST(PcapngFileName, NamesTheExecutableAndItsStartInUtc) {
  EXPECT_EQ(FileName("augustad", kStarted), "augustad-20231114T221320123Z.pcapng");
}

TEST(PcapngFileWriter, WritesTheHeadersThenEveryReportedMessageInOrder) {
  const std::filesystem::path directory = FreshDirectory("writes");
  const std::vector<std::byte> hello = PayloadOf("hello");
  const std::vector<std::byte> world = PayloadOf("world!");
  std::filesystem::path path;
  {
    FileWriter writer(directory, kExecutable, kVersion, kStarted);
    path = writer.Path();
    writer.Report(MessageOf(hello, networking::TapDirection::kOutbound));
    writer.Report(MessageOf(world, networking::TapDirection::kInbound));
  }

  EXPECT_EQ(path, directory / FileName(kExecutable, kStarted));
  const auto blocks = ReadBlocks(path);
  ASSERT_TRUE(blocks.has_value());
  ASSERT_EQ(blocks->size(), 4U);
  EXPECT_EQ((*blocks)[0], EncodeSectionHeader(kExecutable, kVersion));
  EXPECT_EQ((*blocks)[1], EncodeInterfaceDescription());
  EXPECT_EQ((*blocks)[2], EncodeEnhancedPacket(MessageOf(hello, networking::TapDirection::kOutbound)));
  EXPECT_EQ((*blocks)[3], EncodeEnhancedPacket(MessageOf(world, networking::TapDirection::kInbound)));
}

TEST(PcapngFileWriter, AFailedCreateWritesNothingWithOneWarning) {
  // A file where the directory would be.
  const std::filesystem::path root = FreshDirectory("create_failure");
  const std::filesystem::path in_the_way = root / "not_a_directory";
  std::ofstream(in_the_way, std::ios::binary) << "x";
  const LogLines log("create_failure.log");
  const std::vector<std::byte> hello = PayloadOf("hello");
  {
    FileWriter writer(in_the_way, kExecutable, kVersion, kStarted);
    writer.Report(MessageOf(hello, networking::TapDirection::kOutbound));
    writer.Report(MessageOf(hello, networking::TapDirection::kOutbound));
  }

  EXPECT_FALSE(std::filesystem::exists(in_the_way / FileName(kExecutable, kStarted)));
  const std::vector<std::string> warnings = log.Holding("event=pcapng_stopped");
  ASSERT_EQ(warnings.size(), 1U);
  EXPECT_NE(warnings[0].find("WARN"), std::string::npos) << warnings[0];
  EXPECT_NE(warnings[0].find("reason=create_failed"), std::string::npos) << warnings[0];
}

TEST(PcapngFileWriter, AFailedWriteStopsTheFileThereWithOneWarning) {
  const std::filesystem::path directory = FreshDirectory("write_failure");
  const LogLines log("write_failure.log");
  Faults faults;
  // Once only: a writer that went on past it would write the second message.
  faults.Arm(Site::kPcapngWrite, "disk full", 1);
  const std::vector<std::byte> hello = PayloadOf("hello");
  std::filesystem::path path;
  {
    FileWriter writer(directory, kExecutable, kVersion, kStarted, {.faults = &faults});
    path = writer.Path();
    writer.Report(MessageOf(hello, networking::TapDirection::kOutbound));
    writer.Report(MessageOf(hello, networking::TapDirection::kInbound));
  }

  const auto blocks = ReadBlocks(path);
  ASSERT_TRUE(blocks.has_value());
  ASSERT_EQ(blocks->size(), 2U);
  EXPECT_EQ((*blocks)[0], EncodeSectionHeader(kExecutable, kVersion));
  EXPECT_EQ((*blocks)[1], EncodeInterfaceDescription());
  const std::vector<std::string> warnings = log.Holding("event=pcapng_stopped");
  ASSERT_EQ(warnings.size(), 1U);
  EXPECT_NE(warnings[0].find("WARN"), std::string::npos) << warnings[0];
  EXPECT_NE(warnings[0].find("reason=write_failed"), std::string::npos) << warnings[0];
  EXPECT_NE(warnings[0].find("disk full"), std::string::npos) << warnings[0];
}

TEST(PcapngFileWriter, AFullQueueStopsTheFileThereWithOneWarning) {
  const std::filesystem::path directory = FreshDirectory("full_queue");
  const LogLines log("full_queue.log");
  const std::vector<std::byte> hello = PayloadOf("hello");
  std::filesystem::path path;
  {
    // A queue that holds nothing is full at the first message.
    FileWriter writer(directory, kExecutable, kVersion, kStarted, {.queue_capacity = 0});
    path = writer.Path();
    writer.Report(MessageOf(hello, networking::TapDirection::kOutbound));
    writer.Report(MessageOf(hello, networking::TapDirection::kInbound));
  }

  const auto blocks = ReadBlocks(path);
  ASSERT_TRUE(blocks.has_value());
  ASSERT_EQ(blocks->size(), 2U);
  EXPECT_EQ((*blocks)[0], EncodeSectionHeader(kExecutable, kVersion));
  EXPECT_EQ((*blocks)[1], EncodeInterfaceDescription());
  const std::vector<std::string> warnings = log.Holding("event=pcapng_stopped");
  ASSERT_EQ(warnings.size(), 1U);
  EXPECT_NE(warnings[0].find("WARN"), std::string::npos) << warnings[0];
  EXPECT_NE(warnings[0].find("reason=queue_full"), std::string::npos) << warnings[0];
}

}  // namespace
}  // namespace augusta::pcapng
