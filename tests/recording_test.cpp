#include "recording.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/math.h"
#include "augusta/policy_actions.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "content.h"
#include "simulation_mapping.h"

// A match recording (ADR-0048), on the example scenario as the server loads it
// from its golden server pack: its Map, its characters and its Game policy,
// whose last player standing wins. Its replay is tools/replay's, whose tests
// keep their own copy of the scripted match below.
namespace {

using augusta::command::Command;
using augusta::math::Vec3;
using augusta::server::Content;
using augusta::server::RecordedSimulation;
using augusta::server::Recorder;
using augusta::server::Recording;
using augusta::server::RecordingError;
using augusta::server::RecordingHeader;
using augusta::simulation::EntityId;
using augusta::simulation::MatchPlayer;
using augusta::simulation::PlayerCommand;
using augusta::simulation::SessionId;
using augusta::simulation::TickResult;

constexpr std::uint8_t kTickRate = 60;
constexpr float kDeltaTime = 1.0F / kTickRate;

// Long enough for the scripted shooter to kill, at any fire rate the example's Parameters give.
constexpr int kMaxMatchTicks = 600;

const std::filesystem::path kPacks{AUGUSTA_EXAMPLE_PACKS};

augusta::assets::Pack LoadExamplePack() {
  auto pack = augusta::assets::LoadVerifiedPack(kPacks / "server.pack", kPacks / "test.pub");
  if (!pack.has_value()) {
    throw std::runtime_error("the example server pack does not load");
  }
  return *std::move(pack);
}

Content LoadExampleContent() {
  auto content = augusta::server::LoadServerContent(LoadExamplePack(), kTickRate);
  if (!content.has_value()) {
    throw std::runtime_error("the example server pack's content does not load");
  }
  return *std::move(content);
}

RecordingHeader ExampleHeader() {
  return RecordingHeader{.engine_version = std::string(augusta::EngineVersion()),
                         .server_pack = LoadExamplePack().Hash(),
                         .tick_rate_hz = kTickRate};
}

// A World on the example's content, recording to recorder if it is given one.
RecordedSimulation ExampleSimulation(Content content, std::optional<Recorder> recorder) {
  return RecordedSimulation(
      augusta::server::BuildSimulation(content.parameters, kTickRate, content.scenario, std::move(content.policy)),
      std::move(recorder));
}

// The two players of the scripted match, sessions 1 and 2.
constexpr EntityId kFirst{1};
constexpr EntityId kSecond{2};

std::vector<MatchPlayer> ExampleEntrants(const Content& content) {
  const auto characters = augusta::server::ToSimulation(content.scenario.characters);
  const std::string& path = content.scenario.characters.front().path;
  return {
      MatchPlayer{
          .entity = kFirst, .identity = {.session = SessionId{1}, .character = path}, .character = characters.at(path)},
      MatchPlayer{.entity = kSecond,
                  .identity = {.session = SessionId{2}, .character = path},
                  .character = characters.at(path)},
  };
}

// The view, on the angle grid, that looks from from to to.
Command Aiming(const Vec3& from, const Vec3& to) {
  const Vec3 along = to - from;
  Command command;
  command.yaw = augusta::math::SnapAngle(std::atan2(-along.x, -along.z));
  command.pitch = augusta::math::SnapAngle(std::atan2(along.y, std::hypot(along.x, along.z)));
  return command;
}

// The scripted match, a duel: each player stands, looks at the other's chest
// and taps its trigger every tenth tick, until they kill each other on the same
// tick and Game policy ends the Match as a Draw; then the Match is ended in the
// world, as the server ends it after the tick, and one more tick runs. Returns
// every tick's result.
std::vector<TickResult> PlayScriptedMatch(RecordedSimulation& simulation, const Content& content) {
  constexpr int kTapEvery = 10;
  const std::vector<Vec3> spawns = simulation.StartMatch(ExampleEntrants(content), content.scenario.spawn_points);
  const Vec3 eye = content.scenario.characters.front().eye;
  const Vec3 chest = eye * 0.75F;
  const Command first_aim = Aiming(spawns[0] + eye, spawns[1] + chest);
  const Command second_aim = Aiming(spawns[1] + eye, spawns[0] + chest);
  std::vector<TickResult> results;
  augusta::tick::Tick last_tick = 0;
  for (int i = 0; i < kMaxMatchTicks; ++i) {
    Command first = first_aim;
    first.fire = i % kTapEvery == 0;
    first.seen_tick = last_tick;
    Command second = second_aim;
    second.fire = first.fire;
    second.seen_tick = last_tick;
    results.push_back(simulation.Tick(
        {PlayerCommand{.entity = kFirst, .command = first}, PlayerCommand{.entity = kSecond, .command = second}},
        kDeltaTime));
    last_tick = results.back().state.tick;
    if (!results.back().actions.empty()) {
      break;
    }
  }
  simulation.EndMatch();
  results.push_back(simulation.Tick({}, kDeltaTime));
  return results;
}

// The scripted match, recorded.
std::string RecordScriptedMatch() {
  std::ostringstream out(std::ios::binary);
  const Content content = LoadExampleContent();
  RecordedSimulation simulation = ExampleSimulation(LoadExampleContent(), Recorder(out, ExampleHeader()));
  PlayScriptedMatch(simulation, content);
  return std::move(out).str();
}

// A stream buffer whose device fails: reads serve bytes, then fail rather than
// end; writes take up to capacity bytes, then fail.
class FailingBuffer : public std::streambuf {
 public:
  static FailingBuffer Reading(std::string bytes) { return FailingBuffer(std::move(bytes), 0); }
  static FailingBuffer Writing(std::size_t capacity) { return FailingBuffer({}, capacity); }

  [[nodiscard]] const std::string& written() const { return bytes_; }

 protected:
  int_type underflow() override {
    if (served_ == bytes_.size()) {
      throw std::ios_base::failure("the device failed");
    }
    setg(bytes_.data(), bytes_.data() + served_, bytes_.data() + bytes_.size());
    served_ = bytes_.size();
    return traits_type::to_int_type(*gptr());
  }

  int_type overflow(int_type c) override {
    if (traits_type::eq_int_type(c, traits_type::eof())) {
      return traits_type::not_eof(c);
    }
    if (bytes_.size() == capacity_) {
      return traits_type::eof();
    }
    bytes_.push_back(traits_type::to_char_type(c));
    return c;
  }

 private:
  FailingBuffer(std::string bytes, std::size_t capacity) : bytes_(std::move(bytes)), capacity_(capacity) {}

  std::string bytes_;
  std::size_t served_ = 0;
  std::size_t capacity_;
};

Recording Read(const std::string& bytes) {
  std::istringstream in(bytes, std::ios::binary);
  auto recording = augusta::server::ReadRecording(in);
  EXPECT_TRUE(recording.has_value());
  return recording.value_or(Recording{});
}

TEST(RecordingTest, TheScriptedMatchEndsInADraw) {
  const Content content = LoadExampleContent();
  RecordedSimulation simulation = ExampleSimulation(LoadExampleContent(), std::nullopt);
  const std::vector<TickResult> results = PlayScriptedMatch(simulation, content);
  ASSERT_GE(results.size(), 2U);
  const TickResult& ending = results[results.size() - 2];
  ASSERT_EQ(ending.actions.size(), 1U);
  EXPECT_EQ(std::get<augusta::simulation::MatchEnd>(ending.actions.front()).winner, std::nullopt);
  EXPECT_EQ(ending.state.deaths.size(), 2U);
}

TEST(RecordingTest, ARecordingStartsWithItsHeader) { EXPECT_EQ(Read(RecordScriptedMatch()).header, ExampleHeader()); }

TEST(RecordingTest, EveryTickIsRecordedWithTheCommandsItRanOn) {
  const Recording recording = Read(RecordScriptedMatch());
  const Content content = LoadExampleContent();
  RecordedSimulation simulation = ExampleSimulation(LoadExampleContent(), std::nullopt);
  const std::vector<TickResult> results = PlayScriptedMatch(simulation, content);
  ASSERT_EQ(recording.ticks.size(), results.size());
  const auto& first = recording.ticks.front().input;
  ASSERT_EQ(first.commands.size(), 2U);
  EXPECT_EQ(first.commands[0].entity, kFirst);
  EXPECT_TRUE(first.commands[0].command.fire);
  EXPECT_FALSE(recording.ticks[1].input.commands[0].command.fire);
  EXPECT_EQ(first.delta_time, kDeltaTime);
  EXPECT_FALSE(recording.torn);
}

TEST(RecordingTest, AMatchStartIsRecordedBeforeItsFirstTickWithWhereItsPlayersSpawned) {
  const Recording recording = Read(RecordScriptedMatch());
  const auto& first = recording.ticks.front();
  ASSERT_EQ(first.input.match_start.size(), 2U);
  EXPECT_EQ(first.input.match_start[1].entity, kSecond);
  EXPECT_EQ(first.input.match_start[1].identity.session, SessionId{2});
  EXPECT_EQ(first.outcome.spawns.size(), 2U);
  EXPECT_TRUE(recording.ticks[1].input.match_start.empty());
}

TEST(RecordingTest, TheMatchPolicyEndsAndTheEndingOfTheMatchInTheWorldAreRecorded) {
  const Recording recording = Read(RecordScriptedMatch());
  const auto& ending = recording.ticks[recording.ticks.size() - 2];
  ASSERT_TRUE(ending.outcome.match_end.has_value());
  EXPECT_EQ(ending.outcome.match_end->winner, std::nullopt);
  EXPECT_EQ(ending.outcome.deaths.size(), 2U);
  EXPECT_FALSE(ending.outcome.hits.empty());
  EXPECT_TRUE(recording.ticks.back().input.match_ended);
  EXPECT_FALSE(ending.input.match_ended);
}

TEST(RecordingTest, ARemovedBodyIsRecordedBeforeTheTickItLeftOn) {
  std::ostringstream out(std::ios::binary);
  const Content content = LoadExampleContent();
  RecordedSimulation simulation = ExampleSimulation(LoadExampleContent(), Recorder(out, ExampleHeader()));
  simulation.StartMatch(ExampleEntrants(content), content.scenario.spawn_points);
  simulation.Tick({}, kDeltaTime);
  simulation.RemovePlayer(kSecond);
  simulation.Tick({}, kDeltaTime);
  const Recording recording = Read(std::move(out).str());
  ASSERT_EQ(recording.ticks.size(), 2U);
  EXPECT_TRUE(recording.ticks[0].input.removed.empty());
  EXPECT_EQ(recording.ticks[1].input.removed, std::vector<EntityId>{kSecond});
  EXPECT_EQ(recording.ticks[1].outcome.bodies.size(), 1U);
}

TEST(RecordingTest, ALastRecordCutShortIsDroppedAndReported) {
  const std::string bytes = RecordScriptedMatch();
  const Recording whole = Read(bytes);
  const Recording torn = Read(bytes.substr(0, bytes.size() - 3));
  EXPECT_TRUE(torn.torn);
  EXPECT_EQ(torn.ticks.size(), whole.ticks.size() - 1);
}

TEST(RecordingTest, ALastRecordCutShortInItsLengthIsDroppedAndReported) {
  const std::string bytes = RecordScriptedMatch();
  const Recording whole = Read(bytes);
  // Two bytes of a record's length past the last whole tick.
  const Recording torn = Read(bytes + std::string("\x10\x00", 2));
  EXPECT_TRUE(torn.torn);
  EXPECT_EQ(torn.ticks.size(), whole.ticks.size());
}

TEST(RecordingTest, AStreamThatFailsWhereARecordWouldStartIsUnreadableNotWhole) {
  FailingBuffer buffer = FailingBuffer::Reading(RecordScriptedMatch());
  std::istream in(&buffer);
  EXPECT_EQ(augusta::server::ReadRecording(in).error(), RecordingError::kUnreadable);
}

TEST(RecordingTest, AStreamThatFailsPartwayThroughARecordIsUnreadableNotTorn) {
  const std::string bytes = RecordScriptedMatch();
  FailingBuffer buffer = FailingBuffer::Reading(bytes.substr(0, bytes.size() - 3));
  std::istream in(&buffer);
  EXPECT_EQ(augusta::server::ReadRecording(in).error(), RecordingError::kUnreadable);
}

TEST(RecordingTest, AFailedWriteStopsTheRecordingWhichReadsBackUpToItsLastWholeTick) {
  const std::string bytes = RecordScriptedMatch();
  const Recording whole = Read(bytes);
  ASSERT_GE(whole.ticks.size(), 4U);
  // Room for about half the ticks, so the write that fails is partway through one.
  FailingBuffer buffer = FailingBuffer::Writing(bytes.size() / 2);
  std::ostream out(&buffer);
  Recorder recorder(out, whole.header);
  for (const augusta::server::TickRecord& tick : whole.ticks) {
    recorder.Write(tick);
  }
  EXPECT_TRUE(recorder.Stopped());
  EXPECT_EQ(buffer.written().size(), bytes.size() / 2);
  const Recording cut = Read(buffer.written());
  EXPECT_GT(cut.ticks.size(), 0U);
  EXPECT_LT(cut.ticks.size(), whole.ticks.size());
}

TEST(RecordingTest, AFailedHeaderWriteStopsTheRecordingBeforeItsFirstTick) {
  FailingBuffer buffer = FailingBuffer::Writing(2);
  std::ostream out(&buffer);
  Recorder recorder(out, ExampleHeader());
  EXPECT_TRUE(recorder.Stopped());
  recorder.Write(augusta::server::TickRecord{});
  EXPECT_EQ(buffer.written().size(), 2U);
}

TEST(RecordingTest, ARecordingThatIsWrittenWholeIsNotStopped) {
  std::ostringstream out(std::ios::binary);
  Recorder recorder(out, ExampleHeader());
  recorder.Write(augusta::server::TickRecord{});
  EXPECT_FALSE(recorder.Stopped());
}

TEST(RecordingTest, AStreamThatDoesNotStartWithAHeaderIsNoRecording) {
  std::istringstream empty(std::string{}, std::ios::binary);
  EXPECT_EQ(augusta::server::ReadRecording(empty).error(), RecordingError::kNoHeader);
  const std::string garbage("\x02\x00\x00\x00\x09\x09", 6);
  std::istringstream in(garbage, std::ios::binary);
  EXPECT_EQ(augusta::server::ReadRecording(in).error(), RecordingError::kNoHeader);
}

TEST(RecordingTest, ARecordThatDoesNotDecodeIsMalformed) {
  std::string bytes = RecordScriptedMatch();
  // The first tick's type byte, after the header's length and payload and the tick's length.
  const auto header_size = static_cast<std::size_t>(static_cast<unsigned char>(bytes[0]));
  bytes[4 + header_size + 4] = '\x07';
  std::istringstream in(bytes, std::ios::binary);
  EXPECT_EQ(augusta::server::ReadRecording(in).error(), RecordingError::kMalformed);
}

TEST(RecordingTest, AFileThatCannotBeOpenedIsUnreadable) {
  std::ifstream in(std::filesystem::temp_directory_path() / "augusta_no_such_recording.rec", std::ios::binary);
  EXPECT_EQ(augusta::server::ReadRecording(in).error(), RecordingError::kUnreadable);
}

TEST(RecordingTest, ARecordLongerThanAnyTickCanMakeIsMalformedAndNotReadIn) {
  std::string bytes = RecordScriptedMatch();
  const auto header_size = static_cast<std::size_t>(static_cast<unsigned char>(bytes[0]));
  // The first tick's length, made about 2 GB.
  bytes.replace(4 + header_size, 4, std::string("\xFF\xFF\xFF\x7F", 4));
  std::istringstream in(bytes, std::ios::binary);
  EXPECT_EQ(augusta::server::ReadRecording(in).error(), RecordingError::kMalformed);
}

}  // namespace
