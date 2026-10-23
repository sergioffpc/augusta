#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/content.h"
#include "augusta/harness.h"
#include "augusta/host.h"
#include "augusta/pcapng_file_writer.h"
#include "augusta/version.h"
#include "session_test.h"

// The transport's tap through a Host's and a Session's construction options
// (ADR-0076), on a real server host and a real client session over loopback,
// each writing a pcapng file of its own. Built only into a Development build's
// Session tests (ADR-0074), whose session_test.cpp starts the Link.
namespace {

using augusta::harness::Session;
using augusta::harness::SessionConfig;
using augusta::host::Host;
using augusta::host::HostConfig;
using augusta::pcapng::FileWriter;
using augusta::scenario::Scenario;
using augusta::session_test::EmptyWorld;
using augusta::session_test::ExchangeUntil;
using augusta::session_test::kCharacter;
using augusta::session_test::TestHostConfig;
using augusta::session_test::TestSessionConfig;

constexpr std::uint32_t kEnhancedPacketBlock = 6;
// epb_flags' direction bits.
constexpr std::uint32_t kInbound = 1;
constexpr std::uint32_t kOutbound = 2;

std::uint32_t ReadU32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
  }
  return value;
}

// How many of file's Enhanced Packet Blocks are inbound and outbound: its
// blocks walked by the length each states, each packet's direction read from
// the epb_flags option the writer puts first after its data.
struct Directions {
  int inbound = 0;
  int outbound = 0;
};
Directions DirectionsIn(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  const auto bytes = std::as_bytes(std::span(text));
  Directions directions;
  for (std::size_t offset = 0; offset + 8 <= bytes.size(); offset += ReadU32(bytes, offset + 4)) {
    if (ReadU32(bytes, offset) != kEnhancedPacketBlock) {
      continue;
    }
    // Type, length, interface, timestamp (two words), captured and original
    // length, then the data, padded to 4 bytes, then the option's code and length.
    const std::size_t captured = ReadU32(bytes, offset + 20);
    const std::size_t flags = offset + 28 + ((captured + 3) / 4 * 4) + 4;
    const std::uint32_t direction = ReadU32(bytes, flags) & 3U;
    directions.inbound += direction == kInbound ? 1 : 0;
    directions.outbound += direction == kOutbound ? 1 : 0;
  }
  return directions;
}

// A directory of the test's own under the temp folder, empty.
std::filesystem::path FreshDirectory(std::string_view name) {
  const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
  const std::filesystem::path directory =
      std::filesystem::path(testing::TempDir()) / (std::string("augusta_session_tap_test_") + test.name()) / name;
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  return directory;
}

// A client admitted by a server, each given a pcapng writer as its tap, writes
// what it sent and what it received: the client its Join request and the
// server's answers, the server the same from its side.
TEST(SessionTapTest, AClientAndAServerGivenATapEachWriteTheMessagesTheyExchanged) {
  const auto started = std::chrono::system_clock::now();
  auto server_writer =
      std::make_unique<FileWriter>(FreshDirectory("server"), "augustad", augusta::EngineVersion(), started);
  auto client_writer =
      std::make_unique<FileWriter>(FreshDirectory("client"), "augustac", augusta::EngineVersion(), started);
  const std::filesystem::path server_file = server_writer->Path();
  const std::filesystem::path client_file = client_writer->Path();
  {
    HostConfig host_config = TestHostConfig();
    host_config.tap = server_writer.get();
    Host host(host_config,
              Scenario{.collision = {}, .spawn_points = {}, .characters = {{.path = kCharacter, .hitboxes = {}}}});
    SessionConfig session_config = TestSessionConfig(host.ListenEndpoint());
    session_config.tap = client_writer.get();
    Session session(session_config, EmptyWorld());
    session.Connect();

    ASSERT_TRUE(ExchangeUntil(host, {&session}, [&] { return session.GetServerView()->accepted.has_value(); }));
  }
  // Each file is whole once its writer has gone.
  server_writer.reset();
  client_writer.reset();

  const Directions client = DirectionsIn(client_file);
  const Directions server = DirectionsIn(server_file);
  EXPECT_GE(client.outbound, 1);
  EXPECT_GE(client.inbound, 1);
  EXPECT_GE(server.outbound, 1);
  EXPECT_GE(server.inbound, 1);
}

}  // namespace
