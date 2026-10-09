#include <expected>
#include <istream>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "augusta/capture_error.h"
#include "augusta/capture_file.h"
#include "augusta/harness_wire.h"
#include "augusta/protocol.h"
#include "augusta/reenactment.h"
#include "augusta/shared_wire.h"

// A Captured player's part of a Match capture, out of the records
// augusta/capture_file.h reads, which has already held them to the format's
// order: here they are only sorted into what one player keeps.
namespace augusta::harness {

namespace {

// Takes each record of a capture into the Script of one player.
struct ScriptBuilder {
  Script& script;

  void operator()(const protocol::CaptureHeaderWire& /*header*/) const {}

  void operator()(const protocol::CapturedJoinWire& join) const {
    script.spawns.push_back(join.spawn);
    if (join.player == script.player) {
      script.character = join.character;
      script.spawn = join.spawn;
    }
  }

  void operator()(const protocol::CapturedCommandWire& command) const {
    if (command.player == script.player) {
      script.commands.push_back(FromWire(command));
    }
  }

  void operator()(const protocol::CapturedLeaveWire& leave) const {
    if (leave.player == script.player) {
      script.leave = leave.offset;
    }
  }

  void operator()(const protocol::CapturedDeathWire& death) const { script.deaths.push_back(FromWire(death)); }

  void operator()(const protocol::CapturedMatchEndWire& end) const { script.end = FromWire(end); }
};

}  // namespace

std::string DescribeScriptError(const ScriptError& error) {
  return error.capture.has_value() ? std::string(capture_file::DescribeReadError(*error.capture))
                                   : std::string("the capture has no player of that number");
}

std::expected<Script, ScriptError> ReadScript(std::istream& in, CapturedPlayer player) {
  const auto file = capture_file::ReadCaptureFile(in);
  if (!file.has_value()) {
    return std::unexpected(ScriptError{.capture = file.error()});
  }
  Script script;
  script.client_pack = wire::FromWire(file->header.client_pack);
  script.tick_rate_hz = file->header.tick_rate_hz;
  script.player = player;
  script.torn = file->torn;
  for (const protocol::CaptureRecordWire& record : file->records) {
    std::visit(ScriptBuilder{.script = script}, record);
  }
  if (player == 0 || player > script.spawns.size()) {
    return std::unexpected(ScriptError{.capture = std::nullopt});
  }
  return script;
}

}  // namespace augusta::harness
