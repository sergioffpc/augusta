#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/networking.h"
#include "augusta/version.h"
#include "pcapng.h"

// The pcapng of the protocol_decode seeds (tests/fuzz/README.md), a Development
// build's fixture for tools/wireshark's dissector test (ADR-0076): what the
// pcapng encoder writes of every seed in corpus/protocol_decode, one packet per
// seed in name order, naming this build's engine version, so it fails here
// until the fixture is regenerated when a seed, the encoder or the version
// changes. Its packets are on a fixed clock between fixed
// endpoints, the nth (from 0) outbound when n is even and inbound when it is odd.
namespace {

namespace chrono = std::chrono;
using augusta::networking::Ipv4Address;
using augusta::networking::TapDirection;
using augusta::networking::TapEndpoint;
using augusta::networking::TappedMessage;

constexpr std::string_view kExecutable = "augustad";
const TapEndpoint kLocal{.address = Ipv4Address{10, 0, 0, 1}, .port = 27015};
const TapEndpoint kRemote{.address = Ipv4Address{10, 0, 0, 2}, .port = 50000};
// The first packet's time, 2026-10-09T12:00:00Z; each next one is a millisecond later.
constexpr chrono::sys_seconds kFirstPacket = chrono::sys_days{chrono::year{2026} / 10 / 9} + chrono::hours{12};

const std::string kFixturePath = std::string(AUGUSTA_PROTOCOL_DECODE_SEEDS_PCAPNG);

std::vector<std::byte> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> chars{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  std::vector<std::byte> bytes;
  bytes.reserve(chars.size());
  for (const char c : chars) {
    bytes.push_back(static_cast<std::byte>(c));
  }
  return bytes;
}

void Append(std::vector<std::byte>& out, const std::vector<std::byte>& block) {
  out.insert(out.end(), block.begin(), block.end());
}

// What the encoder writes of the seeds, with version in shb_userappl.
std::vector<std::byte> SeedsPcapng(std::string_view version) {
  std::vector<std::filesystem::path> seeds;
  for (const auto& entry : std::filesystem::directory_iterator(AUGUSTA_PROTOCOL_DECODE_CORPUS)) {
    seeds.push_back(entry.path());
  }
  std::ranges::sort(seeds, {}, [](const std::filesystem::path& seed) { return seed.filename().string(); });

  std::vector<std::byte> file = augusta::pcapng::EncodeSectionHeader(kExecutable, version);
  Append(file, augusta::pcapng::EncodeInterfaceDescription());
  for (std::size_t n = 0; n < seeds.size(); ++n) {
    const std::vector<std::byte> payload = ReadFile(seeds[n]);
    Append(file, augusta::pcapng::EncodeEnhancedPacket(TappedMessage{
                     .direction = n % 2 == 0 ? TapDirection::kOutbound : TapDirection::kInbound,
                     .local = kLocal,
                     .remote = kRemote,
                     .payload = payload,
                     .time = kFirstPacket + chrono::milliseconds{static_cast<chrono::milliseconds::rep>(n)},
                 }));
  }
  return file;
}

// The version a fixture's shb_userappl names, after the executable: the
// version of the build that wrote it. Empty when there is none.
std::string WrittenVersion(const std::vector<std::byte>& fixture) {
  // The Section Header Block's fixed fields, then its first option: shb_userappl.
  constexpr std::size_t kOption = 24;
  constexpr std::size_t kValue = kOption + 4;
  if (fixture.size() < kValue) {
    return {};
  }
  const auto u16 = [&](std::size_t at) {
    return static_cast<std::size_t>(std::to_integer<unsigned>(fixture[at]) |
                                    (std::to_integer<unsigned>(fixture[at + 1]) << 8U));
  };
  const std::size_t length = u16(kOption + 2);
  if (fixture.size() < kValue + length) {
    return {};
  }
  std::string userappl;
  for (std::size_t i = kValue; i < kValue + length; ++i) {
    userappl.push_back(static_cast<char>(fixture[i]));
  }
  const std::string prefix = std::string(kExecutable) + " ";
  return userappl.starts_with(prefix) ? userappl.substr(prefix.size()) : std::string{};
}

TEST(ProtocolDecodeSeedsPcapngTest, IsWhatTheEncoderWritesOfTheSeeds) {
  const std::vector<std::byte> fixture = ReadFile(kFixturePath);
  const std::string version = WrittenVersion(fixture);

  ASSERT_FALSE(version.empty()) << kFixturePath << " names no version; regenerate it (tests/fuzz/README.md)";
  EXPECT_EQ(fixture, SeedsPcapng(version)) << "regenerate " << kFixturePath << " (tests/fuzz/README.md)";
}

// What tools/wireshark's test checks capinfos shows: the fixture is of this
// engine version, so a bump means regenerating it, as the cut-release
// runbook's step 3 does.
TEST(ProtocolDecodeSeedsPcapngTest, NamesThisBuildsEngineVersion) {
  EXPECT_EQ(WrittenVersion(ReadFile(kFixturePath)), augusta::EngineVersion())
      << "regenerate " << kFixturePath << " (tests/fuzz/README.md)";
}

// Not a check: rewrites the fixture from the seeds, naming this build's version,
// when a seed or the encoder changes on purpose. Disabled, so ctest never runs
// it; the augusta_protocol_decode_seeds_pcapng build target does
// (tests/fuzz/CMakeLists.txt).
TEST(ProtocolDecodeSeedsPcapngTest, DISABLED_RegenerateFixture) {
  const std::vector<std::byte> file = SeedsPcapng(augusta::EngineVersion());
  std::ofstream out(kFixturePath, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
  ASSERT_TRUE(out.good()) << kFixturePath;
}

}  // namespace
