#include "augusta/capture_records.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

#include "augusta/link_messages.h"
#include "augusta/protocol.h"
#include "shared_core.h"

// A Match capture's records in the engine's types as the protocol's records
// carry them, and back: the format's own rules, "winner 0 is a Draw" and "a
// Join is at offset 0", are kept here.
namespace augusta::link {

namespace {

struct EventToWire {
  std::uint32_t offset;

  protocol::CaptureRecordWire operator()(const capture::Command& command) const {
    // Its Seen time's tick is kept as seen_offset, so it goes as its age 0.
    return protocol::CapturedCommandWire{.command = ToWire(command.command, command.command.seen_tick),
                                         .offset = offset,
                                         .seen_offset = command.seen_offset,
                                         .player = command.player};
  }
  protocol::CaptureRecordWire operator()(const capture::Leave& leave) const {
    return protocol::CapturedLeaveWire{.offset = offset, .player = leave.player};
  }
  protocol::CaptureRecordWire operator()(const capture::Death& death) const {
    return protocol::CapturedDeathWire{.offset = offset, .victim = death.victim, .killer = death.killer};
  }
  protocol::CaptureRecordWire operator()(const capture::MatchEnd& end) const {
    return protocol::CapturedMatchEndWire{.offset = offset, .winner = end.winner.value_or(0)};
  }
};

using Event = std::optional<capture::AtOffset<capture::Event>>;

struct EventFromRecord {
  Event operator()(const protocol::CaptureHeaderWire& /*header*/) const { return std::nullopt; }
  Event operator()(const protocol::CapturedJoinWire& /*join*/) const { return std::nullopt; }
  Event operator()(const protocol::CapturedCommandWire& command) const {
    return capture::AtOffset<capture::Event>{
        .offset = command.offset,
        .event = capture::Command{
            .player = command.player, .seen_offset = command.seen_offset, .command = FromWire(command.command, 0)}};
  }
  Event operator()(const protocol::CapturedLeaveWire& leave) const {
    return capture::AtOffset<capture::Event>{.offset = leave.offset, .event = capture::Leave{.player = leave.player}};
  }
  Event operator()(const protocol::CapturedDeathWire& death) const {
    return capture::AtOffset<capture::Event>{.offset = death.offset,
                                             .event = capture::Death{.victim = death.victim, .killer = death.killer}};
  }
  Event operator()(const protocol::CapturedMatchEndWire& end) const {
    return capture::AtOffset<capture::Event>{
        .offset = end.offset,
        .event =
            capture::MatchEnd{.winner = end.winner == 0 ? std::nullopt : std::optional<capture::Player>(end.winner)}};
  }
};

}  // namespace

protocol::CaptureHeaderWire ToWire(const capture::Header& header) {
  return protocol::CaptureHeaderWire{.server_pack = ToWire(header.server_pack),
                                     .client_pack = ToWire(header.client_pack),
                                     .engine_version = header.engine_version,
                                     .started_unix_ms = header.started.time_since_epoch().count(),
                                     .format_version = protocol::kCaptureFormatVersion,
                                     .tick_rate_hz = header.tick_rate_hz};
}

capture::Header FromWire(const protocol::CaptureHeaderWire& header) {
  return capture::Header{
      .engine_version = header.engine_version,
      .server_pack = FromWire(header.server_pack),
      .client_pack = FromWire(header.client_pack),
      .tick_rate_hz = header.tick_rate_hz,
      .started = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(header.started_unix_ms))};
}

protocol::CaptureRecordWire ToWire(capture::Player player, const capture::Join& join) {
  return protocol::CapturedJoinWire{.spawn = join.spawn,
                                    .offset = 0,
                                    .session = static_cast<protocol::SessionIdWire>(join.session),
                                    .character = join.character,
                                    .player = player};
}

capture::Join FromWire(const protocol::CapturedJoinWire& join) {
  return capture::Join{.session = std::to_underlying(join.session), .character = join.character, .spawn = join.spawn};
}

protocol::CaptureRecordWire ToWire(const capture::AtOffset<capture::Event>& event) {
  return std::visit(EventToWire{.offset = event.offset}, event.event);
}

std::optional<capture::AtOffset<capture::Event>> EventFromWire(const protocol::CaptureRecordWire& record) {
  return std::visit(EventFromRecord{}, record);
}

}  // namespace augusta::link
