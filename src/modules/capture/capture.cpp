#include "augusta/capture.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <ios>
#include <istream>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/failure.h"
#include "augusta/protocol.h"
#include "augusta/shared_wire.h"
#include "frame.h"

namespace augusta::capture {

namespace {

// How reading the next frame ended.
enum class Frame : std::uint8_t {
  kRead,
  // The stream ended where a frame would start.
  kEnd,
  // The stream ended partway through a frame.
  kTorn,
  // The stream failed: what it holds past here is unknown, so neither an end
  // nor a torn frame can be told from it.
  kUnreadable,
};

// Reads the next frame from in into payload: its length in one byte, then it.
Frame ReadFrame(std::istream& in, std::vector<std::byte>& payload) {
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

std::expected<void, ReadError> ReadMagic(std::istream& in) {
  std::array<char, protocol::kCaptureMagic.size()> magic{};
  in.read(magic.data(), magic.size());
  if (in.bad()) {
    return std::unexpected(ReadError::kUnreadable);
  }
  if (static_cast<std::size_t>(in.gcount()) != magic.size() ||
      !std::ranges::equal(magic, protocol::kCaptureMagic,
                          [](char read, std::byte expected) { return static_cast<std::byte>(read) == expected; })) {
    return std::unexpected(ReadError::kNotACapture);
  }
  return {};
}

std::expected<protocol::CaptureHeaderWire, ReadError> ReadHeader(std::istream& in) {
  std::vector<std::byte> payload;
  const Frame frame = ReadFrame(in, payload);
  if (frame == Frame::kUnreadable) {
    return std::unexpected(ReadError::kUnreadable);
  }
  if (frame != Frame::kRead) {
    return std::unexpected(ReadError::kUnsupported);
  }
  const auto record = protocol::DecodeCaptureRecord(payload);
  const auto* header = record.has_value() ? std::get_if<protocol::CaptureHeaderWire>(&*record) : nullptr;
  if (header == nullptr || header->format_version != protocol::kCaptureFormatVersion) {
    return std::unexpected(ReadError::kUnsupported);
  }
  return *header;
}

// The engine's types as the protocol's records carry them, and back: the
// format's own rules, "winner 0 is a Draw" and "a Join is at offset 0", are
// kept here.

protocol::CaptureHeaderWire ToWire(const Header& header) {
  return protocol::CaptureHeaderWire{.server_pack = wire::ToWire(header.server_pack),
                                     .client_pack = wire::ToWire(header.client_pack),
                                     .engine_version = header.engine_version,
                                     .started_unix_ms = header.started.time_since_epoch().count(),
                                     .format_version = protocol::kCaptureFormatVersion,
                                     .tick_rate_hz = header.tick_rate_hz};
}

Header FromWire(const protocol::CaptureHeaderWire& header) {
  return Header{
      .engine_version = header.engine_version,
      .server_pack = wire::FromWire(header.server_pack),
      .client_pack = wire::FromWire(header.client_pack),
      .tick_rate_hz = header.tick_rate_hz,
      .started = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(header.started_unix_ms))};
}

protocol::CaptureRecordWire ToWire(Player player, const Join& join) {
  return protocol::CapturedJoinWire{.spawn = join.spawn,
                                    .offset = 0,
                                    .session = static_cast<protocol::SessionIdWire>(join.session),
                                    .character = join.character,
                                    .player = player};
}

struct EventToWire {
  std::uint32_t offset;

  protocol::CaptureRecordWire operator()(const Command& command) const {
    // Its Seen time's tick is kept as seen_offset, so it goes as its age 0.
    return protocol::CapturedCommandWire{.command = wire::ToWire(command.command, command.command.seen_tick),
                                         .offset = offset,
                                         .seen_offset = command.seen_offset,
                                         .player = command.player};
  }
  protocol::CaptureRecordWire operator()(const Leave& leave) const {
    return protocol::CapturedLeaveWire{.offset = offset, .player = leave.player};
  }
  protocol::CaptureRecordWire operator()(const Death& death) const {
    return protocol::CapturedDeathWire{.offset = offset, .victim = death.victim, .killer = death.killer};
  }
  protocol::CaptureRecordWire operator()(const MatchEnd& end) const {
    return protocol::CapturedMatchEndWire{.offset = offset, .winner = end.winner.value_or(0)};
  }
};

// Sorts each record after the header into a capture's Joins or events: the
// file's order has already put every Join first, numbered from 1.
struct RecordFromWire {
  Capture& capture;

  void operator()(const protocol::CaptureHeaderWire& /*header*/) const {}
  void operator()(const protocol::CapturedJoinWire& join) const {
    capture.joins.push_back(
        Join{.session = std::to_underlying(join.session), .character = join.character, .spawn = join.spawn});
  }
  void operator()(const protocol::CapturedCommandWire& command) const {
    capture.events.push_back({.offset = command.offset,
                              .event = Command{.player = command.player,
                                               .seen_offset = command.seen_offset,
                                               .command = wire::FromWire(command.command, 0)}});
  }
  void operator()(const protocol::CapturedLeaveWire& leave) const {
    capture.events.push_back({.offset = leave.offset, .event = Leave{.player = leave.player}});
  }
  void operator()(const protocol::CapturedDeathWire& death) const {
    capture.events.push_back({.offset = death.offset, .event = Death{.victim = death.victim, .killer = death.killer}});
  }
  void operator()(const protocol::CapturedMatchEndWire& end) const {
    capture.events.push_back(
        {.offset = end.offset,
         .event = MatchEnd{.winner = end.winner == 0 ? std::nullopt : std::optional<Player>(end.winner)}});
  }
};

// The frame of record, or why it has none (Framed).
std::expected<std::vector<std::byte>, EncodeError> Encode(const protocol::CaptureRecordWire& record) {
  return Framed(protocol::EncodeCaptureRecord(record), protocol::TypeOf(record));
}

// Decision: whether each record may follow those before it (ADR-0050): every
// Join first, numbered from 1 in order and at offset 0, then the events in
// offset order, each naming a player a Join did, and nothing after the Match
// end.
class RecordOrder {
 public:
  bool Admits(const protocol::CaptureRecordWire& record) { return std::visit(*this, record); }

  bool operator()(const protocol::CaptureHeaderWire& /*header*/) const { return false; }

  bool operator()(const protocol::CapturedJoinWire& join) {
    if (events_ || join.offset != 0 || join.player != joins_ + 1U) {
      return false;
    }
    ++joins_;
    return true;
  }

  bool operator()(const protocol::CapturedCommandWire& command) { return Follows(command.offset, {command.player}); }

  bool operator()(const protocol::CapturedLeaveWire& leave) { return Follows(leave.offset, {leave.player}); }

  bool operator()(const protocol::CapturedDeathWire& death) {
    return Follows(death.offset, {death.victim, death.killer});
  }

  bool operator()(const protocol::CapturedMatchEndWire& end) {
    if (end.winner != 0 && !Known(end.winner)) {
      return false;
    }
    const bool follows = Follows(end.offset, {});
    ended_ = true;
    return follows;
  }

 private:
  [[nodiscard]] bool Known(std::uint8_t player) const { return player >= 1 && player <= joins_; }

  // Whether an event at offset naming players may come next.
  bool Follows(std::uint32_t offset, std::initializer_list<std::uint8_t> players) {
    if (ended_ || offset < last_offset_ || !std::ranges::all_of(players, [this](std::uint8_t p) { return Known(p); })) {
      return false;
    }
    events_ = true;
    last_offset_ = offset;
    return true;
  }

  std::size_t joins_ = 0;
  // Whether an event has come, after which no Join may; the last one's
  // offset; whether the Match end has come, after which nothing may.
  bool events_ = false;
  std::uint32_t last_offset_ = 0;
  bool ended_ = false;
};

// A capture file's records, as the protocol decodes them.
struct CaptureFile {
  protocol::CaptureHeaderWire header;
  // Every record after the header, in the order written.
  std::vector<protocol::CaptureRecordWire> records;
  // Whether the file ended partway through a record, which was dropped.
  bool torn = false;
};

// Reads the capture in, held to the format's order, in the protocol's types. A
// last record cut short is dropped and reported in CaptureFile::torn; a stream
// that fails, wherever it does, is kUnreadable, never a whole or torn capture.
std::expected<CaptureFile, ReadError> ReadCaptureFile(std::istream& in) {
  if (!in) {
    return std::unexpected(ReadError::kUnreadable);
  }
  if (auto magic = ReadMagic(in); !magic.has_value()) {
    return std::unexpected(magic.error());
  }
  auto header = ReadHeader(in);
  if (!header.has_value()) {
    return std::unexpected(header.error());
  }
  CaptureFile file{.header = *std::move(header), .records = {}, .torn = false};
  RecordOrder order;
  std::vector<std::byte> payload;
  for (Frame frame = ReadFrame(in, payload); frame != Frame::kEnd; frame = ReadFrame(in, payload)) {
    if (frame == Frame::kUnreadable) {
      return std::unexpected(ReadError::kUnreadable);
    }
    if (frame == Frame::kTorn) {
      file.torn = true;
      break;
    }
    auto record = protocol::DecodeCaptureRecord(payload);
    if (!record.has_value() || !order.Admits(*record)) {
      return std::unexpected(ReadError::kMalformed);
    }
    file.records.push_back(*std::move(record));
  }
  return file;
}

}  // namespace

std::string_view DescribeReadError(ReadError error) {
  switch (error) {
    case ReadError::kUnreadable:
      return "the capture could not be read";
    case ReadError::kNotACapture:
      return "the file does not start with a capture's magic";
    case ReadError::kUnsupported:
      return "the capture's header is missing or of a format version this engine does not read";
    case ReadError::kMalformed:
      return "a record of the capture does not decode or is out of order";
  }
  return "unknown capture error";
}

std::expected<std::vector<std::byte>, EncodeError> Framed(
    const std::expected<protocol::BytesWire, protocol::EncodeError>& payload, protocol::CaptureRecordTypeWire type) {
  if (!payload.has_value()) {
    return std::unexpected(failure::Failure{
        .code = failure::Code::kInvariantViolated,
        .context = {{.key = "capture_record_type", .value = std::to_string(static_cast<std::uint8_t>(type))}},
        .detail = std::string(protocol::DescribeEncodeError(payload.error()))});
  }
  if (payload->size() > kMaxFramePayload) {
    return std::unexpected(RecordTooLong{});
  }
  std::vector<std::byte> frame;
  frame.reserve(kFrameLengthSize + payload->size());
  frame.push_back(static_cast<std::byte>(payload->size()));
  frame.insert(frame.end(), payload->begin(), payload->end());
  return frame;
}

bool StartsWithMagic(std::istream& in) { return ReadMagic(in).has_value(); }

std::expected<Capture, ReadError> ReadCapture(std::istream& in) {
  auto file = ReadCaptureFile(in);
  if (!file.has_value()) {
    return std::unexpected(file.error());
  }
  Capture capture{.header = FromWire(file->header), .joins = {}, .events = {}, .torn = file->torn};
  capture.events.reserve(file->records.size());
  for (const protocol::CaptureRecordWire& record : file->records) {
    std::visit(RecordFromWire{.capture = capture}, record);
  }
  return capture;
}

std::expected<Encoded, EncodeError> EncodeHeader(const Header& header) {
  auto frame = Encode(ToWire(header));
  if (!frame.has_value()) {
    return std::unexpected(std::move(frame.error()));
  }
  std::vector<std::byte> bytes(protocol::kCaptureMagic.begin(), protocol::kCaptureMagic.end());
  bytes.insert(bytes.end(), frame->begin(), frame->end());
  return Encoded(std::move(bytes));
}

std::expected<Encoded, EncodeError> EncodeJoin(Player player, const Join& join) {
  return Encode(ToWire(player, join)).transform([](std::vector<std::byte> bytes) { return Encoded(std::move(bytes)); });
}

std::expected<Encoded, EncodeError> EncodeEvent(const AtOffset<Event>& event) {
  return Encode(std::visit(EventToWire{.offset = event.offset}, event.event))
      .transform([](std::vector<std::byte> bytes) { return Encoded(std::move(bytes)); });
}

void Write(std::ostream& out, const Encoded& encoded) {
  out.write(reinterpret_cast<const char*>(encoded.bytes_.data()), static_cast<std::streamsize>(encoded.bytes_.size()));
}

std::expected<void, EncodeError> WriteCapture(std::ostream& out, const Capture& capture) {
  std::vector<Encoded> pieces;
  pieces.reserve(1 + capture.joins.size() + capture.events.size());
  auto header = EncodeHeader(capture.header);
  if (!header.has_value()) {
    return std::unexpected(std::move(header.error()));
  }
  pieces.push_back(*std::move(header));
  for (std::size_t i = 0; i < capture.joins.size(); ++i) {
    auto join = EncodeJoin(static_cast<Player>(i + 1), capture.joins[i]);
    if (!join.has_value()) {
      return std::unexpected(std::move(join.error()));
    }
    pieces.push_back(*std::move(join));
  }
  for (const AtOffset<Event>& event : capture.events) {
    auto encoded = EncodeEvent(event);
    if (!encoded.has_value()) {
      return std::unexpected(std::move(encoded.error()));
    }
    pieces.push_back(*std::move(encoded));
  }
  for (const Encoded& piece : pieces) {
    Write(out, piece);
  }
  return {};
}

}  // namespace augusta::capture
