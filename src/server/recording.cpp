#include "recording.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <ios>
#include <istream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/failure.h"
#include "augusta/faults.h"
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

// Where a recording lost a record: the step a Failure's context names.
enum class Step : std::uint8_t {
  kWrite,
  kFlush,
  // The record found the queue full: the disk is not keeping up.
  kQueueFull,
  // The record is longer than a reader takes.
  kRecordTooLong,
};

std::string_view StepName(Step step) {
  switch (step) {
    case Step::kWrite:
      return "write";
    case Step::kFlush:
      return "flush";
    case Step::kQueueFull:
      return "queue_full";
    case Step::kRecordTooLong:
      return "record_too_long";
  }
  return "unknown";
}

// Decision: what losing tick's record at step is in mode. An optional
// recording's loss is its own subsystem's; a strict one's is the runtime's.
failure::Failure LostRecording(RecordingMode mode, tick::Tick tick, Step step, std::string detail) {
  failure::Code code = failure::Code::kStrictRecordingFailed;
  if (mode == RecordingMode::kOptional) {
    code = step == Step::kFlush ? failure::Code::kRecordingFlushFailed : failure::Code::kRecordingWriteFailed;
  }
  return {.code = code,
          .context = {{.key = "tick", .value = std::to_string(tick)},
                      {.key = "step", .value = std::string(StepName(step))}},
          .detail = std::move(detail)};
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

std::string_view RecordingModeName(RecordingMode mode) {
  switch (mode) {
    case RecordingMode::kOptional:
      return "optional";
    case RecordingMode::kStrict:
      return "strict";
  }
  return "unknown";
}

std::string_view RecordingStateName(RecordingState state) {
  switch (state) {
    case RecordingState::kEnabled:
      return "enabled";
    case RecordingState::kDegraded:
      return "degraded";
    case RecordingState::kStopped:
      return "stopped";
  }
  return "unknown";
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

// The thread a Recorder writes its stream on, the records queued for it,
// oldest first, and the recording's state. The Simulation thread only ever
// waits on the lock, which the writer holds to take a record and never across
// a write.
class Recorder::Writer {
 public:
  Writer(std::ostream& out, protocol::BytesWire header, RecorderOptions options)
      : out_(&out),
        mode_(options.mode),
        faults_(options.faults),
        on_state_(std::move(options.on_state)),
        capacity_(options.capacity) {
    Enter(RecordingState::kEnabled);
    queue_.push_back({.tick = 0, .payload = std::move(header)});
    thread_ = std::thread([this] { Run(); });
  }

  // Writes what is still queued before it goes, then stops the recording.
  ~Writer() {
    {
      const std::scoped_lock lock(mutex_);
      closing_ = true;
    }
    ready_.notify_one();
    thread_.join();
    const std::scoped_lock lock(mutex_);
    if (state_ != RecordingState::kStopped) {
      LI("subsystem=server event=recording_stopped mode={} ticks={} lost={}", RecordingModeName(mode_),
         records_written_ == 0 ? std::size_t{0} : records_written_ - 1, failure_.has_value());
      Enter(RecordingState::kStopped);
    }
  }

  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;
  Writer(Writer&&) = delete;
  Writer& operator=(Writer&&) = delete;

  // Queues tick's payload, or loses the recording there when capacity records
  // are still unwritten. Once it has lost one, drops every one.
  void Push(tick::Tick tick, protocol::BytesWire payload) {
    {
      const std::scoped_lock lock(mutex_);
      if (state_ != RecordingState::kEnabled) {
        return;
      }
      // Dropping this tick and going on would leave a recording whose ticks
      // are not their places; losing it here keeps every one before it.
      if (queue_.size() >= capacity_) {
        LoseLocked(tick, Step::kQueueFull, {});
        return;
      }
      queue_.push_back({.tick = tick, .payload = std::move(payload)});
    }
    ready_.notify_one();
  }

  void Lose(tick::Tick tick, Step step) {
    const std::scoped_lock lock(mutex_);
    LoseLocked(tick, step, {});
  }

  void WaitUntilWritten() {
    std::unique_lock lock(mutex_);
    written_.wait(lock, [this] { return queue_.empty() && !writing_; });
  }

  [[nodiscard]] RecordingState State() const { return state_; }

  [[nodiscard]] std::optional<failure::Failure> Failure() const {
    const std::scoped_lock lock(mutex_);
    return failure_;
  }

 private:
  struct Queued {
    tick::Tick tick;
    protocol::BytesWire payload;
  };

  struct Loss {
    Step step;
    std::string detail;
  };

  std::optional<std::string> Trip(failure::Site site) const {
    return faults_ == nullptr ? std::nullopt : faults_->Trip(site);
  }

  // Mechanism: writes and flushes payload's frame, or says which step the
  // stream failed at. An injected fault fails the stream, as the disk would.
  std::optional<Loss> Persist(const protocol::BytesWire& payload) {
    if (std::optional<std::string> fault = Trip(failure::Site::kRecordingWrite)) {
      out_->setstate(std::ios::badbit);
      return Loss{.step = Step::kWrite, .detail = *std::move(fault)};
    }
    WriteFrame(*out_, payload);
    if (!*out_) {
      return Loss{.step = Step::kWrite, .detail = {}};
    }
    if (std::optional<std::string> fault = Trip(failure::Site::kRecordingFlush)) {
      out_->setstate(std::ios::badbit);
      return Loss{.step = Step::kFlush, .detail = *std::move(fault)};
    }
    out_->flush();
    if (!*out_) {
      return Loss{.step = Step::kFlush, .detail = {}};
    }
    return std::nullopt;
  }

  void Run() {
    std::unique_lock lock(mutex_);
    while (true) {
      ready_.wait(lock, [this] { return closing_ || !queue_.empty(); });
      if (queue_.empty()) {
        return;
      }
      const Queued record = std::move(queue_.front());
      queue_.pop_front();
      writing_ = true;
      lock.unlock();
      std::optional<Loss> loss = Persist(record.payload);
      lock.lock();
      writing_ = false;
      if (loss) {
        // Whatever of the record reached the file reads back as a torn last
        // tick; writing on would put whole records after it that no reader
        // reaches.
        queue_.clear();
        LoseLocked(record.tick, loss->step, std::move(loss->detail));
      } else {
        ++records_written_;
      }
      if (queue_.empty()) {
        written_.notify_all();
      }
    }
  }

  // The recording's first loss, at tick's step: kept, reported once, and
  // nothing more queued. A later one changes nothing. With mutex_ held.
  void LoseLocked(tick::Tick tick, Step step, std::string detail) {
    if (state_ != RecordingState::kEnabled) {
      return;
    }
    failure_ = LostRecording(mode_, tick, step, std::move(detail));
    if (mode_ == RecordingMode::kOptional) {
      // This is the boundary that recovers it: the run goes on without it.
      LE("subsystem=server event=recording_degraded {}", failure::DescribeFailure(*failure_));
      Enter(RecordingState::kDegraded);
    } else {
      // Only the state change: the runtime that stops on the failure writes
      // its one ERR line, with its detail (ADR-0033).
      LI("subsystem=server event=recording_stopped mode={} tick={} step={}", RecordingModeName(mode_), tick,
         StepName(step));
      Enter(RecordingState::kStopped);
    }
  }

  void Enter(RecordingState state) {
    state_ = state;
    if (on_state_) {
      on_state_(state);
    }
  }

  std::ostream* out_;
  const RecordingMode mode_;
  failure::Faults* const faults_;
  const std::function<void(RecordingState)> on_state_;
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  // Notified each time the queue is found empty, its last record written.
  std::condition_variable written_;
  std::deque<Queued> queue_;
  bool closing_ = false;
  // A record off the queue but not yet written keeps WaitUntilWritten waiting.
  bool writing_ = false;
  // The header among them. Writer thread only.
  std::size_t records_written_ = 0;
  // Written under mutex_, read without it by any thread.
  std::atomic<RecordingState> state_ = RecordingState::kEnabled;
  std::optional<failure::Failure> failure_;
  // Declared last, so it is joined before what it writes from goes.
  std::thread thread_;
};

Recorder::Recorder(std::ostream& out, const RecordingHeader& header, RecorderOptions options)
    : writer_(std::make_unique<Writer>(out, protocol::EncodeRecord(ToWire(header)), std::move(options))) {}

Recorder::~Recorder() = default;
Recorder::Recorder(Recorder&&) noexcept = default;
Recorder& Recorder::operator=(Recorder&&) noexcept = default;

void Recorder::Write(const TickRecord& tick) {
  if (State() != RecordingState::kEnabled) {
    return;
  }
  protocol::BytesWire payload = protocol::EncodeRecord(ToWire(tick));
  // A record ReadRecording would refuse would make every tick after it
  // unreadable; losing it here keeps the file readable up to it.
  if (payload.size() > kMaxRecordSize) {
    writer_->Lose(tick.outcome.tick, Step::kRecordTooLong);
    return;
  }
  writer_->Push(tick.outcome.tick, std::move(payload));
}

void Recorder::WaitUntilWritten() { writer_->WaitUntilWritten(); }

RecordingState Recorder::State() const { return writer_->State(); }

std::optional<failure::Failure> Recorder::Failure() const {
  // The Simulation thread asks every tick: a recording still enabled has
  // lost nothing, which the state says without the writer's lock.
  if (State() == RecordingState::kEnabled) {
    return std::nullopt;
  }
  return writer_->Failure();
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

void RecordedSimulation::WaitUntilRecorded() {
  if (recorder_.has_value()) {
    recorder_->WaitUntilWritten();
  }
}

std::optional<failure::Failure> RecordedSimulation::RecordingFailure() const {
  return recorder_.has_value() ? recorder_->Failure() : std::nullopt;
}

}  // namespace augusta::server
