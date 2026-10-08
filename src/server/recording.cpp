#include "recording.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <ios>
#include <istream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/failure.h"
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

// Logs, once per recording, that it stopped on tick, and why.
void LogStopped(tick::Tick tick, std::string_view reason) {
  LE("subsystem=server event=recording_stopped tick={} reason=\"{}\"", tick, reason);
}

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

// The thread a Recorder writes its stream on, and the records queued for it,
// oldest first. The Simulation thread only ever waits on the lock, which the
// writer holds to take a record and never across a write.
class Recorder::Writer {
 public:
  Writer(std::ostream& out, protocol::BytesWire header, std::size_t capacity) : out_(&out), capacity_(capacity) {
    queue_.push_back({.tick = 0, .payload = std::move(header)});
    thread_ = std::thread([this] { Run(); });
  }

  // Writes what is still queued before it goes.
  ~Writer() {
    {
      const std::scoped_lock lock(mutex_);
      closing_ = true;
    }
    ready_.notify_one();
    thread_.join();
  }

  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;
  Writer(Writer&&) = delete;
  Writer& operator=(Writer&&) = delete;

  // Whether tick's payload was taken: not when capacity records are still
  // unwritten. Once a write has failed, it takes every one and drops it.
  bool Push(tick::Tick tick, protocol::BytesWire payload) {
    {
      const std::scoped_lock lock(mutex_);
      if (failed_) {
        return true;
      }
      if (queue_.size() >= capacity_) {
        return false;
      }
      queue_.push_back({.tick = tick, .payload = std::move(payload)});
    }
    ready_.notify_one();
    return true;
  }

  // Whether a write the stream failed has stopped the recording.
  [[nodiscard]] bool Failed() const { return failed_; }

 private:
  struct Queued {
    tick::Tick tick;
    protocol::BytesWire payload;
  };

  void Run() {
    std::unique_lock lock(mutex_);
    while (true) {
      ready_.wait(lock, [this] { return closing_ || !queue_.empty(); });
      if (queue_.empty()) {
        return;
      }
      const Queued record = std::move(queue_.front());
      queue_.pop_front();
      lock.unlock();
      WriteFrame(*out_, record.payload);
      out_->flush();
      const bool failed = !*out_;
      lock.lock();
      // Whatever of the record reached the file reads back as a torn last
      // tick; writing on would put whole records after it that no reader
      // reaches.
      if (failed) {
        LogStopped(record.tick, "write failed");
        failed_ = true;
        queue_.clear();
      }
    }
  }

  std::ostream* out_;
  std::size_t capacity_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Queued> queue_;
  bool closing_ = false;
  // Written under mutex_, read without it by the Simulation thread.
  std::atomic<bool> failed_ = false;
  // Declared last, so it is joined before what it writes from goes.
  std::thread thread_;
};

namespace {

// header's record. One the protocol cannot carry is the recording server's own
// bug, found before the recording starts, when a failure may still throw.
protocol::BytesWire HeaderRecord(const RecordingHeader& header) {
  auto record = EncodeToRecord(ToWire(header));
  if (!record.has_value()) {
    throw std::runtime_error("server::Recorder: " + failure::DescribeFailure(record.error()));
  }
  return *std::move(record);
}

}  // namespace

Recorder::Recorder(std::ostream& out, const RecordingHeader& header, std::size_t capacity)
    : writer_(std::make_unique<Writer>(out, HeaderRecord(header), capacity)) {}

Recorder::~Recorder() = default;
Recorder::Recorder(Recorder&&) noexcept = default;
Recorder& Recorder::operator=(Recorder&&) noexcept = default;

std::expected<void, failure::Failure> Recorder::Write(const TickRecord& tick) {
  if (Stopped()) {
    return {};
  }
  auto payload = EncodeToRecord(ToWire(tick));
  // Not the recording's failure but the server's own bug, which stops the
  // runtime, and is reported once by whoever stops it: the recording just
  // ends here, every tick before it whole.
  if (!payload.has_value()) {
    stopped_ = true;
    payload.error().context.push_back({.key = "tick", .value = std::to_string(tick.outcome.tick)});
    return std::unexpected(std::move(payload.error()));
  }
  // A record ReadRecording would refuse would make every tick after it
  // unreadable; stopping here keeps the file readable up to it.
  if (payload->size() > kMaxRecordSize) {
    Stop(tick.outcome.tick, "record too long");
    return {};
  }
  // Dropping this tick and going on would leave a recording whose ticks are
  // not their places; stopping leaves every one before it.
  if (!writer_->Push(tick.outcome.tick, *std::move(payload))) {
    Stop(tick.outcome.tick, "disk fell behind");
  }
  return {};
}

bool Recorder::Stopped() const { return stopped_ || writer_->Failed(); }

void Recorder::Stop(tick::Tick tick, std::string_view reason) {
  LogStopped(tick, reason);
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

const std::optional<failure::Failure>& RecordedSimulation::Failure() const { return failure_; }

simulation::TickResult RecordedSimulation::Tick(const std::vector<simulation::PlayerCommand>& commands,
                                                float delta_time) {
  simulation::TickResult result = world_.Tick(commands, delta_time);
  if (recorder_.has_value()) {
    pending_.commands = commands;
    pending_.delta_time = delta_time;
    auto written =
        recorder_->Write(TickRecord{.input = std::move(pending_), .outcome = OutcomeOf(pending_spawns_, result)});
    if (!written.has_value() && !failure_.has_value()) {
      failure_ = std::move(written.error());
    }
  }
  pending_ = TickInput{};
  pending_spawns_.clear();
  return result;
}

}  // namespace augusta::server
