#include "augusta/capture.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <ios>
#include <istream>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/capture_records.h"
#include "augusta/failure.h"
#include "augusta/link_messages.h"
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
  std::array<char, link::kCaptureMagic.size()> magic{};
  in.read(magic.data(), magic.size());
  if (in.bad()) {
    return std::unexpected(ReadError::kUnreadable);
  }
  if (static_cast<std::size_t>(in.gcount()) != magic.size() ||
      !std::ranges::equal(magic, link::kCaptureMagic,
                          [](char read, std::byte expected) { return static_cast<std::byte>(read) == expected; })) {
    return std::unexpected(ReadError::kNotACapture);
  }
  return {};
}

std::expected<Header, ReadError> ReadHeader(std::istream& in) {
  std::vector<std::byte> payload;
  const Frame frame = ReadFrame(in, payload);
  if (frame == Frame::kUnreadable) {
    return std::unexpected(ReadError::kUnreadable);
  }
  if (frame != Frame::kRead) {
    return std::unexpected(ReadError::kUnsupported);
  }
  // A header of another format version does not decode.
  const auto record = link::DecodeCaptureRecord(payload);
  const auto* header = record.has_value() ? std::get_if<Header>(&*record) : nullptr;
  if (header == nullptr) {
    return std::unexpected(ReadError::kUnsupported);
  }
  return *header;
}

// Decision: whether each record may follow those before it (ADR-0050): every
// Join first, numbered from 1 in order and at offset 0, then the events in
// offset order, each naming a player a Join did, and nothing after the Match
// end.
class RecordOrder {
 public:
  bool Admits(const link::CaptureRecord& record) { return std::visit(*this, record); }

  bool operator()(const Header& /*header*/) const { return false; }

  bool operator()(const link::CapturedJoin& join) {
    if (events_ || join.offset != 0 || join.player != joins_ + 1U) {
      return false;
    }
    ++joins_;
    return true;
  }

  bool operator()(const AtOffset<Event>& event) {
    return std::visit([this, offset = event.offset](const auto& what) { return AdmitsEvent(offset, what); },
                      event.event);
  }

 private:
  bool AdmitsEvent(std::uint32_t offset, const Command& command) { return Follows(offset, {command.player}); }

  bool AdmitsEvent(std::uint32_t offset, const Leave& leave) { return Follows(offset, {leave.player}); }

  bool AdmitsEvent(std::uint32_t offset, const Death& death) { return Follows(offset, {death.victim, death.killer}); }

  bool AdmitsEvent(std::uint32_t offset, const MatchEnd& end) {
    if (end.winner.has_value() && !Known(*end.winner)) {
      return false;
    }
    const bool follows = Follows(offset, {});
    ended_ = true;
    return follows;
  }

  [[nodiscard]] bool Known(Player player) const { return player >= 1 && player <= joins_; }

  // Whether an event at offset naming players may come next.
  bool Follows(std::uint32_t offset, std::initializer_list<Player> players) {
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

// A capture file's records, as the Link decodes them.
struct CaptureFile {
  Header header;
  // Every record after the header, in the order written.
  std::vector<link::CaptureRecord> records;
  // Whether the file ended partway through a record, which was dropped.
  bool torn = false;
};

// Reads the capture in, held to the format's order, record by record. A
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
    auto record = link::DecodeCaptureRecord(payload);
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
    const std::expected<std::vector<std::byte>, failure::Failure>& payload) {
  if (!payload.has_value()) {
    return std::unexpected(payload.error());
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
  Capture capture{.header = file->header, .joins = {}, .events = {}, .torn = file->torn};
  capture.events.reserve(file->records.size());
  // The file's order has already put every Join first, numbered from 1, and
  // left no header after the first.
  for (link::CaptureRecord& record : file->records) {
    if (auto* join = std::get_if<link::CapturedJoin>(&record)) {
      capture.joins.push_back(std::move(join->join));
    } else if (auto* event = std::get_if<AtOffset<Event>>(&record)) {
      capture.events.push_back(std::move(*event));
    }
  }
  return capture;
}

std::expected<Encoded, EncodeError> EncodeHeader(const Header& header) {
  auto frame = Framed(link::EncodeCaptureHeader(header));
  if (!frame.has_value()) {
    return std::unexpected(std::move(frame.error()));
  }
  std::vector<std::byte> bytes(link::kCaptureMagic.begin(), link::kCaptureMagic.end());
  bytes.insert(bytes.end(), frame->begin(), frame->end());
  return Encoded(std::move(bytes));
}

std::expected<Encoded, EncodeError> EncodeJoin(Player player, const Join& join) {
  return Framed(link::EncodeCaptureJoin(player, join)).transform([](std::vector<std::byte> bytes) {
    return Encoded(std::move(bytes));
  });
}

std::expected<Encoded, EncodeError> EncodeEvent(const AtOffset<Event>& event) {
  return Framed(link::EncodeCaptureEvent(event)).transform([](std::vector<std::byte> bytes) {
    return Encoded(std::move(bytes));
  });
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
