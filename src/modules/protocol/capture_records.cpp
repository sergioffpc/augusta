#include "capture_records.h"

#include <optional>

#include "augusta.pb.h"
#include "augusta/grid.h"
#include "augusta/protocol.h"
#include "decoder.h"
#include "encoder.h"

namespace augusta::protocol {

void RecordEncoder::operator()(const CaptureHeaderWire& wire) const {
  pb::CaptureHeader& header = *record.mutable_header();
  header.set_server_pack(BytesOf(wire.server_pack));
  header.set_client_pack(BytesOf(wire.client_pack));
  header.set_engine_version(out.Text(wire.engine_version, kMaxEngineVersionLength));
  header.set_started_unix_ms(wire.started_unix_ms);
  header.set_tick_rate_hz(wire.tick_rate_hz);
  header.set_match(BytesOf(wire.match));
}

void RecordEncoder::operator()(const CapturedJoinWire& wire) const {
  pb::CapturedJoin& join = *record.mutable_join();
  join.set_offset(wire.offset);
  join.set_player(wire.player);
  join.set_session(BytesOf(wire.session));
  join.set_character(out.Text(wire.character, kMaxCharacterNameLength));
  join.set_spawn_x(Steps(wire.spawn.x, math::kPositionGrid));
  join.set_spawn_y(Steps(wire.spawn.y, math::kPositionGrid));
  join.set_spawn_z(Steps(wire.spawn.z, math::kPositionGrid));
}

void RecordEncoder::operator()(const CapturedCommandWire& wire) const {
  pb::CapturedCommand& command = *record.mutable_command();
  command.set_offset(wire.offset);
  command.set_player(wire.player);
  command.set_seen_offset(wire.seen_offset);
  Write(wire.command, *command.mutable_command(), out);
}

void RecordEncoder::operator()(const CapturedLeaveWire& wire) const {
  pb::CapturedLeave& leave = *record.mutable_leave();
  leave.set_offset(wire.offset);
  leave.set_player(wire.player);
}

void RecordEncoder::operator()(const CapturedDeathWire& wire) const {
  pb::CapturedDeath& death = *record.mutable_death();
  death.set_offset(wire.offset);
  death.set_victim(wire.victim);
  death.set_killer(wire.killer);
}

void RecordEncoder::operator()(const CapturedMatchEndWire& wire) const {
  pb::CapturedMatchEnd& end = *record.mutable_match_end();
  end.set_offset(wire.offset);
  end.set_winner(wire.winner);
}

namespace {

CaptureHeaderWire Read(const pb::CaptureHeader& header, Inbound& in) {
  return CaptureHeaderWire{.server_pack = in.PackHash(header.server_pack()),
                           .client_pack = in.PackHash(header.client_pack()),
                           .engine_version = in.Text(header.engine_version(), kMaxEngineVersionLength),
                           .started_unix_ms = header.started_unix_ms(),
                           .match = in.Uuid(header.match()),
                           .tick_rate_hz = in.Narrow(header.tick_rate_hz())};
}

CapturedJoinWire Read(const pb::CapturedJoin& join, Inbound& in) {
  return CapturedJoinWire{.spawn = in.FromSteps(join.spawn_x(), join.spawn_y(), join.spawn_z(), math::kPositionGrid),
                          .offset = join.offset(),
                          .session = in.Uuid(join.session()),
                          .character = in.Text(join.character(), kMaxCharacterNameLength),
                          .player = in.Narrow(join.player())};
}

CapturedCommandWire Read(const pb::CapturedCommand& command, Inbound& in) {
  return CapturedCommandWire{.command = Read(command.command(), in),
                             .offset = command.offset(),
                             .seen_offset = command.seen_offset(),
                             .player = in.Narrow(command.player())};
}

}  // namespace

std::optional<CaptureRecordWire> ReadRecord(const pb::CaptureRecord& record, Inbound& in) {
  switch (record.record_case()) {
    case pb::CaptureRecord::kHeader:
      return Read(record.header(), in);
    case pb::CaptureRecord::kJoin:
      return Read(record.join(), in);
    case pb::CaptureRecord::kCommand:
      return Read(record.command(), in);
    case pb::CaptureRecord::kLeave:
      return CapturedLeaveWire{.offset = record.leave().offset(), .player = in.Narrow(record.leave().player())};
    case pb::CaptureRecord::kDeath:
      return CapturedDeathWire{.offset = record.death().offset(),
                               .victim = in.Narrow(record.death().victim()),
                               .killer = in.Narrow(record.death().killer())};
    case pb::CaptureRecord::kMatchEnd:
      return CapturedMatchEndWire{.offset = record.match_end().offset(),
                                  .winner = in.Narrow(record.match_end().winner())};
    case pb::CaptureRecord::RECORD_NOT_SET:
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace augusta::protocol
