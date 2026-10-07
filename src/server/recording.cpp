#include "recording.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ios>
#include <istream>
#include <optional>
#include <ostream>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/grid.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/policy_actions.h"
#include "augusta/protocol.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "wire.h"

namespace augusta::server {

namespace {

// A record's length goes before it, in this many bytes, least significant first.
constexpr std::size_t kLengthSize = 4;
constexpr int kBitsPerByte = 8;
constexpr std::size_t kByteMask = 0xFFU;

void WriteFrame(std::ostream& out, const protocol::BytesWire& payload) {
  std::array<char, kLengthSize> length{};
  for (std::size_t i = 0; i < kLengthSize; ++i) {
    length[i] = static_cast<char>((payload.size() >> (kBitsPerByte * i)) & kByteMask);
  }
  out.write(length.data(), length.size());
  out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
}

// How reading the next record's frame ended.
enum class Frame : std::uint8_t {
  kRead,
  // The stream ended where a frame would start.
  kEnd,
  // The stream ended partway through a frame.
  kTorn,
  // Its length is more than any record's.
  kTooLong,
  // The stream failed: what it holds past here is unknown, so neither an end
  // nor a torn record can be told from it.
  kUnreadable,
};

Frame ReadFrame(std::istream& in, protocol::BytesWire& payload) {
  std::array<char, kLengthSize> length_bytes{};
  in.read(length_bytes.data(), length_bytes.size());
  if (in.bad()) {
    return Frame::kUnreadable;
  }
  const auto length_read = static_cast<std::size_t>(in.gcount());
  if (length_read == 0) {
    return Frame::kEnd;
  }
  if (length_read < kLengthSize) {
    return Frame::kTorn;
  }
  std::size_t length = 0;
  for (std::size_t i = 0; i < kLengthSize; ++i) {
    length |= static_cast<std::size_t>(static_cast<unsigned char>(length_bytes[i])) << (kBitsPerByte * i);
  }
  if (length > kMaxRecordSize) {
    return Frame::kTooLong;
  }
  payload.resize(length);
  in.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(length));
  if (in.bad()) {
    return Frame::kUnreadable;
  }
  const auto payload_read = static_cast<std::size_t>(in.gcount());
  return payload_read == length ? Frame::kRead : Frame::kTorn;
}

}  // namespace

TickOutcome OutcomeOf(const std::vector<math::Vec3>& spawns, const simulation::TickResult& result) {
  TickOutcome outcome{
      .tick = result.state.tick,
      .spawns = {},
      .bodies = result.state.bodies,
      .shots = result.state.shots,
      .hits = result.state.hits,
      .deaths = result.state.deaths,
      .match_end = std::nullopt,
  };
  // A Spawn point need not be on the grid; the body placed there is, and so is
  // what Match start tells every client.
  outcome.spawns.reserve(spawns.size());
  for (const math::Vec3& spawn : spawns) {
    outcome.spawns.push_back(math::SnapPosition(spawn));
  }
  for (const simulation::PolicyAction& action : result.actions) {
    if (const auto* end = std::get_if<simulation::MatchEnd>(&action)) {
      outcome.match_end = *end;
    }
  }
  return outcome;
}

std::string_view DescribeRecordingError(RecordingError error) {
  switch (error) {
    case RecordingError::kUnreadable:
      return "the recording could not be read";
    case RecordingError::kNoHeader:
      return "the file does not start with a recording's header";
    case RecordingError::kMalformed:
      return "a record of the recording does not decode";
  }
  return "unknown recording error";
}

Recorder::Recorder(std::ostream& out, const RecordingHeader& header) : out_(&out) {
  WriteFrame(*out_, protocol::EncodeRecord(ToWire(header)));
  out_->flush();
  if (!*out_) {
    Stop(0, "write failed");
  }
}

void Recorder::Write(const TickRecord& tick) {
  if (stopped_) {
    return;
  }
  const protocol::BytesWire payload = protocol::EncodeRecord(ToWire(tick));
  // A record ReadRecording would refuse would make every tick after it
  // unreadable; stopping here keeps the file readable up to it.
  if (payload.size() > kMaxRecordSize) {
    Stop(tick.outcome.tick, "record too long");
    return;
  }
  WriteFrame(*out_, payload);
  out_->flush();
  // Whatever of the record reached the file reads back as a torn last tick;
  // writing on would put whole records after it that no reader reaches.
  if (!*out_) {
    Stop(tick.outcome.tick, "write failed");
  }
}

void Recorder::Stop(tick::Tick tick, std::string_view reason) {
  LE("subsystem=server event=recording_stopped tick={} reason=\"{}\"", tick, reason);
  stopped_ = true;
}

std::expected<Recording, RecordingError> ReadRecording(std::istream& in) {
  if (!in) {
    return std::unexpected(RecordingError::kUnreadable);
  }
  protocol::BytesWire payload;
  const Frame header_frame = ReadFrame(in, payload);
  if (header_frame == Frame::kUnreadable) {
    return std::unexpected(RecordingError::kUnreadable);
  }
  if (header_frame != Frame::kRead) {
    return std::unexpected(RecordingError::kNoHeader);
  }
  const auto header = protocol::DecodeRecord(payload);
  if (!header.has_value() || !std::holds_alternative<protocol::RecordingHeaderWire>(*header)) {
    return std::unexpected(RecordingError::kNoHeader);
  }
  Recording recording{.header = FromWire(std::get<protocol::RecordingHeaderWire>(*header)), .ticks = {}, .torn = false};
  for (Frame frame = ReadFrame(in, payload); frame != Frame::kEnd; frame = ReadFrame(in, payload)) {
    if (frame == Frame::kUnreadable) {
      return std::unexpected(RecordingError::kUnreadable);
    }
    if (frame == Frame::kTorn) {
      recording.torn = true;
      break;
    }
    if (frame == Frame::kTooLong) {
      return std::unexpected(RecordingError::kMalformed);
    }
    const auto record = protocol::DecodeRecord(payload);
    if (!record.has_value() || !std::holds_alternative<protocol::RecordedTickWire>(*record)) {
      return std::unexpected(RecordingError::kMalformed);
    }
    recording.ticks.push_back(
        FromWire(std::get<protocol::RecordedTickWire>(*record), static_cast<tick::Tick>(recording.ticks.size() + 1)));
  }
  return recording;
}

RecordedSimulation::RecordedSimulation(simulation::World world, std::optional<Recorder> recorder)
    : world_(std::move(world)), recorder_(std::move(recorder)) {}

void RecordedSimulation::EndMatch() {
  world_.EndMatch();
  pending_.match_ended = true;
}

void RecordedSimulation::RemovePlayer(simulation::EntityId entity) {
  world_.RemovePlayer(entity);
  pending_.removed.push_back(entity);
}

std::vector<math::Vec3> RecordedSimulation::StartMatch(const std::vector<simulation::MatchPlayer>& players,
                                                       const std::vector<math::Vec3>& spawn_points) {
  std::vector<math::Vec3> spawns = world_.StartMatch(players, spawn_points);
  pending_.match_start.clear();
  for (const simulation::MatchPlayer& player : players) {
    pending_.match_start.push_back(RecordedEntrant{.entity = player.entity, .identity = player.identity});
  }
  pending_spawns_ = spawns;
  return spawns;
}

simulation::TickResult RecordedSimulation::Tick(const std::vector<simulation::PlayerCommand>& commands,
                                                float delta_time) {
  simulation::TickResult result = world_.Tick(commands, delta_time);
  if (recorder_.has_value()) {
    pending_.commands = commands;
    pending_.delta_time = delta_time;
    recorder_->Write(TickRecord{.input = std::move(pending_), .outcome = OutcomeOf(pending_spawns_, result)});
  }
  pending_ = TickInput{};
  pending_spawns_.clear();
  return result;
}

}  // namespace augusta::server
