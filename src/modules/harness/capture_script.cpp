#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <istream>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/harness_wire.h"
#include "augusta/protocol.h"
#include "augusta/reenactment.h"

// Reads a Match capture as server::Capturer writes it (ADR-0050): its magic,
// then each record's payload after its length in one byte, the header first.
// The server reads its own captures in server/capture.cpp; a client cannot
// link the server's code, and needs only one player's part of a capture.
namespace augusta::harness {

namespace {

// How reading the next record's frame ended.
enum class Frame : std::uint8_t { kRead, kEnd, kTorn, kUnreadable };

// Reads the next record's payload, after its one-byte length, into payload.
Frame ReadFrame(std::istream& in, protocol::BytesWire& payload) {
  char length = 0;
  in.read(&length, 1);
  if (in.bad()) {
    return Frame::kUnreadable;
  }
  if (in.gcount() == 0) {
    return Frame::kEnd;
  }
  payload.resize(static_cast<unsigned char>(length));
  in.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
  if (in.bad()) {
    return Frame::kUnreadable;
  }
  return static_cast<std::size_t>(in.gcount()) == payload.size() ? Frame::kRead : Frame::kTorn;
}

std::expected<void, ScriptError> ReadMagic(std::istream& in) {
  std::array<char, protocol::kCaptureMagic.size()> magic{};
  in.read(magic.data(), magic.size());
  if (in.bad()) {
    return std::unexpected(ScriptError::kUnreadable);
  }
  if (static_cast<std::size_t>(in.gcount()) != magic.size() ||
      !std::ranges::equal(magic, protocol::kCaptureMagic,
                          [](char read, std::byte expected) { return static_cast<std::byte>(read) == expected; })) {
    return std::unexpected(ScriptError::kNotACapture);
  }
  return {};
}

std::expected<protocol::CaptureHeaderWire, ScriptError> ReadHeader(std::istream& in) {
  protocol::BytesWire payload;
  const Frame frame = ReadFrame(in, payload);
  if (frame == Frame::kUnreadable) {
    return std::unexpected(ScriptError::kUnreadable);
  }
  if (frame != Frame::kRead) {
    return std::unexpected(ScriptError::kUnsupported);
  }
  const auto record = protocol::DecodeCaptureRecord(payload);
  const auto* header = record.has_value() ? std::get_if<protocol::CaptureHeaderWire>(&*record) : nullptr;
  if (header == nullptr || header->format_version != protocol::kCaptureFormatVersion) {
    return std::unexpected(ScriptError::kUnsupported);
  }
  return *header;
}

// A Script as its records are read: the order they must come in (every Join
// first, numbered from 1, then the events in offset order, nothing after the
// Match end), and the part of them its player keeps.
class ScriptBuilder {
 public:
  ScriptBuilder(const protocol::CaptureHeaderWire& header, CapturedPlayer player) {
    script_.client_pack = FromWire(header.client_pack);
    script_.tick_rate_hz = header.tick_rate_hz;
    script_.player = player;
  }

  // Takes record in; false if it may not follow the records before it.
  bool Add(const protocol::CaptureRecordWire& record) { return std::visit(*this, record); }

  bool operator()(const protocol::CaptureHeaderWire& /*header*/) { return false; }

  bool operator()(const protocol::CapturedJoinWire& join) {
    if (events_ || join.offset != 0 || join.player != script_.spawns.size() + 1) {
      return false;
    }
    script_.spawns.push_back(join.spawn);
    if (join.player == script_.player) {
      script_.character = join.character;
      script_.spawn = join.spawn;
    }
    return true;
  }

  bool operator()(const protocol::CapturedCommandWire& command) {
    if (!Follows(command.offset, {command.player})) {
      return false;
    }
    if (command.player == script_.player) {
      script_.commands.push_back(FromWire(command));
    }
    return true;
  }

  bool operator()(const protocol::CapturedLeaveWire& leave) {
    if (!Follows(leave.offset, {leave.player})) {
      return false;
    }
    if (leave.player == script_.player) {
      script_.leave = leave.offset;
    }
    return true;
  }

  bool operator()(const protocol::CapturedDeathWire& death) {
    if (!Follows(death.offset, {death.victim, death.killer})) {
      return false;
    }
    script_.deaths.push_back(FromWire(death));
    return true;
  }

  bool operator()(const protocol::CapturedMatchEndWire& end) {
    if (!Follows(end.offset, {}) || (end.winner != 0 && !Known(end.winner))) {
      return false;
    }
    script_.end = FromWire(end);
    return true;
  }

  // The Script, once every record is in; kNoSuchPlayer if no Join numbered its player.
  std::expected<Script, ScriptError> Finish(bool torn) && {
    if (script_.player == 0 || script_.player > script_.spawns.size()) {
      return std::unexpected(ScriptError::kNoSuchPlayer);
    }
    script_.torn = torn;
    return std::move(script_);
  }

 private:
  [[nodiscard]] bool Known(CapturedPlayer player) const { return player >= 1 && player <= script_.spawns.size(); }

  // Whether an event at offset naming players may follow the records so far.
  bool Follows(std::uint32_t offset, std::initializer_list<CapturedPlayer> players) {
    if (script_.end.has_value() || offset < last_offset_ ||
        !std::ranges::all_of(players, [this](CapturedPlayer p) { return Known(p); })) {
      return false;
    }
    events_ = true;
    last_offset_ = offset;
    return true;
  }

  Script script_;
  // Whether an event has been read, after which no Join may come, and the offset of the last.
  bool events_ = false;
  std::uint32_t last_offset_ = 0;
};

}  // namespace

std::string_view DescribeScriptError(ScriptError error) {
  switch (error) {
    case ScriptError::kUnreadable:
      return "the capture could not be read";
    case ScriptError::kNotACapture:
      return "the file does not start with a capture's magic";
    case ScriptError::kUnsupported:
      return "the capture's header is missing or of a format version this engine does not read";
    case ScriptError::kMalformed:
      return "a record of the capture does not decode or is out of order";
    case ScriptError::kNoSuchPlayer:
      return "the capture has no player of that number";
  }
  return "unknown capture error";
}

std::expected<Script, ScriptError> ReadScript(std::istream& in, CapturedPlayer player) {
  if (!in) {
    return std::unexpected(ScriptError::kUnreadable);
  }
  if (auto magic = ReadMagic(in); !magic.has_value()) {
    return std::unexpected(magic.error());
  }
  const auto header = ReadHeader(in);
  if (!header.has_value()) {
    return std::unexpected(header.error());
  }
  ScriptBuilder builder(*header, player);
  protocol::BytesWire payload;
  for (Frame frame = ReadFrame(in, payload); frame != Frame::kEnd; frame = ReadFrame(in, payload)) {
    if (frame == Frame::kUnreadable) {
      return std::unexpected(ScriptError::kUnreadable);
    }
    if (frame == Frame::kTorn) {
      return std::move(builder).Finish(/*torn=*/true);
    }
    const auto record = protocol::DecodeCaptureRecord(payload);
    if (!record.has_value() || !builder.Add(*record)) {
      return std::unexpected(ScriptError::kMalformed);
    }
  }
  return std::move(builder).Finish(/*torn=*/false);
}

}  // namespace augusta::harness
