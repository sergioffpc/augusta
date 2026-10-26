#include "augusta/capturer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "augusta/capture.h"
#include "augusta/capture_records.h"
#include "augusta/capture_retention.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/grid.h"
#include "augusta/match.h"
#include "augusta/tick.h"
#include "capture_writer.h"

namespace augusta::host {

namespace {

// What an optional capture's loss at step is: a slow disk, an oversized
// record and a full budget are named apart from a disk that refused the file.
failure::Code OptionalLossCode(CaptureStep step) {
  switch (step) {
    case CaptureStep::kFlush:
      return failure::Code::kCaptureFlushFailed;
    case CaptureStep::kQueueFull:
      return failure::Code::kCaptureQueueFull;
    case CaptureStep::kRecordTooLong:
      return failure::Code::kCaptureRecordTooLong;
    case CaptureStep::kRetentionBudget:
      return failure::Code::kCaptureRetentionBudget;
    case CaptureStep::kCreate:
    case CaptureStep::kWrite:
      return failure::Code::kCaptureWriteFailed;
  }
  return failure::Code::kCaptureWriteFailed;
}

// How many ticks to is after from: negative when it is before.
std::int64_t Offset(tick::Tick from, tick::Tick to) {
  return static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
}

}  // namespace

std::string_view CaptureStopName(CaptureStop stop) {
  switch (stop) {
    case CaptureStop::kRecordTooLong:
      return "record_too_long";
    case CaptureStop::kQueueFull:
      return "queue_full";
    case CaptureStop::kWriteFailed:
      return "write_failed";
    case CaptureStop::kRetentionBudget:
      return "retention_budget";
  }
  return "unknown";
}

std::string_view CaptureStepName(CaptureStep step) {
  switch (step) {
    case CaptureStep::kCreate:
      return "create";
    case CaptureStep::kWrite:
      return "write";
    case CaptureStep::kFlush:
      return "flush";
    case CaptureStep::kQueueFull:
      return "queue_full";
    case CaptureStep::kRecordTooLong:
      return "record_too_long";
    case CaptureStep::kRetentionBudget:
      return "retention_budget";
  }
  return "unknown";
}

failure::Failure LostCapture(CaptureMode mode, const boost::uuids::uuid& match, CaptureStep step, std::string detail) {
  return {.code = mode == CaptureMode::kStrict ? failure::Code::kStrictCaptureFailed : OptionalLossCode(step),
          .context = {{.key = "match", .value = boost::uuids::to_string(match)},
                      {.key = "step", .value = std::string(CaptureStepName(step))}},
          .detail = std::move(detail)};
}

std::string_view CaptureModeName(CaptureMode mode) {
  switch (mode) {
    case CaptureMode::kOptional:
      return "optional";
    case CaptureMode::kStrict:
      return "strict";
  }
  return "unknown";
}

std::string_view CaptureHealthName(CaptureHealth health) {
  switch (health) {
    case CaptureHealth::kEnabled:
      return "enabled";
    case CaptureHealth::kDegraded:
      return "degraded";
    case CaptureHealth::kStopped:
      return "stopped";
  }
  return "unknown";
}

Capturer::Capturer(std::filesystem::path directory, capture::Header header, CaptureOptions options)
    : directory_(std::move(directory)),
      header_(std::move(header)),
      writer_(std::make_unique<Writer>(directory_, std::move(options))) {}

Capturer::~Capturer() = default;

void Capturer::StartMatch(const boost::uuids::uuid& match, const std::vector<CaptureEntrant>& players,
                          tick::Tick first_tick, std::chrono::system_clock::time_point started) {
  if (open_) {
    writer_->Close(match_);
    open_ = false;
  }
  // A strict capture that lost a record captures nothing more.
  if (Health() == CaptureHealth::kStopped) {
    return;
  }
  match_ = match;
  open_ = true;
  writer_->Begin(match_);
  first_tick_ = first_tick;
  players_.clear();
  sessions_.clear();
  capture::Header header = header_;
  header.match = match_;
  header.started = std::chrono::floor<std::chrono::milliseconds>(started);
  std::optional<capture::Encoded> encoded = Admit(capture::EncodeHeader(header));
  if (!encoded.has_value()) {
    // No file is opened: its Joins and the rest are dropped by the writer.
    return;
  }
  writer_->Open(match_, directory_ / CaptureFileName(header.started, match_), *std::move(encoded));
  for (const CaptureEntrant& entrant : players) {
    const auto player = static_cast<capture::Player>(players_.size() + 1);
    players_.emplace(entrant.entity, player);
    sessions_.emplace(entrant.session, player);
    Queue(capture::EncodeJoin(player, capture::Join{.session = entrant.session,
                                                    .character = entrant.character,
                                                    .spawn = math::SnapPosition(entrant.spawn)}));
  }
}

void Capturer::Command(tick::Tick tick, EntityId entity, const command::Command& command) {
  const capture::Player player = PlayerOf(entity);
  if (!open_ || player == 0) {
    return;
  }
  const std::int64_t seen =
      std::clamp<std::int64_t>(Offset(first_tick_, command.seen_tick), std::numeric_limits<std::int32_t>::min(),
                               std::numeric_limits<std::int32_t>::max());
  command::Command captured = command;
  captured.seen_tick = 0;
  Queue(capture::EncodeEvent(
      {.offset = OffsetOf(tick),
       .event =
           capture::Command{.player = player, .seen_offset = static_cast<std::int32_t>(seen), .command = captured}}));
}

void Capturer::Leave(tick::Tick tick, EntityId entity) {
  const capture::Player player = PlayerOf(entity);
  if (!open_ || player == 0) {
    return;
  }
  Queue(capture::EncodeEvent({.offset = OffsetOf(tick), .event = capture::Leave{.player = player}}));
}

void Capturer::Death(tick::Tick tick, EntityId victim, EntityId killer) {
  const capture::Player dead = PlayerOf(victim);
  if (!open_ || dead == 0) {
    return;
  }
  Queue(capture::EncodeEvent(
      {.offset = OffsetOf(tick), .event = capture::Death{.victim = dead, .killer = PlayerOf(killer)}}));
}

void Capturer::EndMatch(tick::Tick last_tick, std::optional<SessionId> winner) {
  if (!open_) {
    return;
  }
  std::optional<capture::Player> winning;
  if (winner.has_value()) {
    if (const auto found = sessions_.find(*winner); found != sessions_.end()) {
      winning = found->second;
    }
  }
  Queue(capture::EncodeEvent({.offset = OffsetOf(last_tick), .event = capture::MatchEnd{.winner = winning}}));
  writer_->Close(match_);
  open_ = false;
  players_.clear();
  sessions_.clear();
}

void Capturer::WaitUntilWritten() { writer_->WaitUntilWritten(); }

void Capturer::Finish() {
  writer_->Finish();
  open_ = false;
}

CaptureHealth Capturer::Health() const { return writer_->Health(); }

std::optional<failure::Failure> Capturer::Loss() const {
  // The Simulation thread asks every tick: captures still enabled have lost
  // nothing, which the health says without the writer's lock.
  if (Health() == CaptureHealth::kEnabled) {
    return std::nullopt;
  }
  return writer_->Loss();
}

const std::optional<failure::Failure>& Capturer::Failure() const { return failure_; }

capture::Player Capturer::PlayerOf(EntityId entity) const {
  const auto found = players_.find(entity);
  return found == players_.end() ? capture::Player{0} : found->second;
}

std::uint32_t Capturer::OffsetOf(tick::Tick tick) const {
  return static_cast<std::uint32_t>(
      std::clamp<std::int64_t>(Offset(first_tick_, tick), 0, std::numeric_limits<std::uint32_t>::max()));
}

std::optional<capture::Encoded> Capturer::Admit(std::expected<capture::Encoded, capture::EncodeError> encoded) {
  if (encoded.has_value()) {
    return *std::move(encoded);
  }
  if (auto* broken = std::get_if<failure::Failure>(&encoded.error())) {
    // Not the capture's loss but the server's own bug, which stops the runtime
    // (ADR-0033): kept for Host to take, and nothing of it written.
    if (!failure_.has_value()) {
      failure_ = std::move(*broken);
    }
    return std::nullopt;
  }
  // A record capture::ReadCapture would refuse would make every record after
  // it unreadable; stopping here keeps the file readable up to it.
  writer_->Stop(match_, CaptureStep::kRecordTooLong);
  return std::nullopt;
}

void Capturer::Queue(std::expected<capture::Encoded, capture::EncodeError> encoded) {
  if (std::optional<capture::Encoded> admitted = Admit(std::move(encoded))) {
    writer_->Push(match_, *std::move(admitted));
  }
}

}  // namespace augusta::host
