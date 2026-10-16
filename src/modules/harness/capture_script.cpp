#include <cstdint>
#include <expected>
#include <istream>
#include <optional>
#include <string>
#include <variant>

#include "augusta/capture.h"
#include "augusta/capture_error.h"
#include "augusta/reenactment.h"

// A Captured player's part of a Match capture, out of the capture
// capture::ReadCapture reads, which has already held it to the format's
// rules: here its events are only sorted into what one player keeps.
namespace augusta::harness {

namespace {

// Takes each event of a capture, at offset, into the Script of one player.
struct ScriptBuilder {
  Script& script;
  std::uint32_t offset;

  void operator()(const capture::Command& command) const {
    if (command.player == script.player) {
      script.commands.push_back({.offset = offset, .event = command});
    }
  }

  void operator()(const capture::Leave& leave) const {
    if (leave.player == script.player) {
      script.leave = offset;
    }
  }

  void operator()(const capture::Death& death) const { script.deaths.push_back({.offset = offset, .event = death}); }

  void operator()(const capture::MatchEnd& end) const { script.end = {.offset = offset, .event = end}; }
};

}  // namespace

std::string DescribeScriptError(const ScriptError& error) {
  return error.capture.has_value() ? std::string(capture::DescribeReadError(*error.capture))
                                   : std::string("the capture has no player of that number");
}

std::expected<Script, ScriptError> ReadScript(std::istream& in, capture::Player player) {
  const auto read = capture::ReadCapture(in);
  if (!read.has_value()) {
    return std::unexpected(ScriptError{.capture = read.error()});
  }
  if (player == 0 || player > read->joins.size()) {
    return std::unexpected(ScriptError{.capture = std::nullopt});
  }
  const capture::Join& join = read->joins[player - 1];
  Script script{.client_pack = read->header.client_pack,
                .tick_rate_hz = read->header.tick_rate_hz,
                .player = player,
                .character = join.character,
                .spawn = join.spawn,
                .spawns = {},
                .commands = {},
                .leave = std::nullopt,
                .deaths = {},
                .end = std::nullopt,
                .torn = read->torn};
  for (const capture::Join& each : read->joins) {
    script.spawns.push_back(each.spawn);
  }
  for (const capture::AtOffset<capture::Event>& event : read->events) {
    std::visit(ScriptBuilder{.script = script, .offset = event.offset}, event.event);
  }
  return script;
}

}  // namespace augusta::harness
