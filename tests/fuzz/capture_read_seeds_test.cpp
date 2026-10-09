#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/math.h"
#include "capture.h"
#include "match.h"

// The capture_read fuzz target's seed (tests/fuzz/README.md): one whole
// capture holding every kind of record, so the fuzzer starts from a valid file
// rather than from nothing. It is what a Capturer writes today, so a change to
// the format fails here until the seed is regenerated.
namespace {

using augusta::server::CaptureEntrant;
using augusta::server::CaptureHeader;
using augusta::server::Capturer;
using augusta::server::EntityId;
using augusta::server::SessionId;

const std::string kSeedPath = std::string(AUGUSTA_CAPTURE_READ_CORPUS) + "/match";

// The bytes of a short Match a Capturer captured: two players, a Command
// each, a Death, a Leave and a win.
std::string Captured() {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / ("augusta_capture_seed_" + std::to_string(std::random_device{}()));
  std::filesystem::create_directories(directory);
  {
    CaptureHeader header{.engine_version = "2.0.1", .tick_rate_hz = 60, .started = {}};
    header.server_pack.fill(std::byte{1});
    header.client_pack.fill(std::byte{2});
    Capturer capturer(directory, header);
    capturer.StartMatch(
        {CaptureEntrant{.session = SessionId{7}, .entity = EntityId{70}, .character = "soldier", .spawn = {1, 0, 2}},
         CaptureEntrant{.session = SessionId{8}, .entity = EntityId{80}, .character = "sniper", .spawn = {-3, 0, 4}}},
        1000, std::chrono::system_clock::time_point{std::chrono::milliseconds{1'791'540'900'123}});
    augusta::command::Command command;
    command.movement.direction = augusta::math::Vec3{0.6F, 0.0F, -0.8F};
    command.movement.sprint = true;
    command.fire = true;
    command.yaw = 1.5F;
    command.seen_tick = 998;
    command.seen_fraction = 0.75F;
    capturer.Command(1000, EntityId{70}, command);
    capturer.Command(1001, EntityId{80}, command);
    capturer.Death(1005, EntityId{80}, EntityId{70});
    capturer.Leave(1006, EntityId{80});
    capturer.EndMatch(1006, SessionId{7});
  }
  const std::filesystem::path file = *std::filesystem::directory_iterator(directory);
  std::ifstream in(file, std::ios::binary);
  std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  in.close();
  std::filesystem::remove_all(directory);
  return bytes;
}

TEST(CaptureReadSeedsTest, TheSeedIsWhatACapturerWrites) {
  std::ifstream in(kSeedPath, std::ios::binary);
  const std::string seed{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  EXPECT_EQ(seed, Captured()) << "regenerate the seeds (tests/fuzz/README.md)";
}

// Not a check: rewrites the seed when the format changes on purpose. Disabled,
// so ctest never runs it; the augusta_capture_read_seeds build target does
// (tests/fuzz/CMakeLists.txt).
TEST(CaptureReadSeedsTest, DISABLED_RegenerateSeeds) {
  const std::string bytes = Captured();
  std::ofstream out(kSeedPath, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  ASSERT_TRUE(out.good()) << kSeedPath;
}

}  // namespace
