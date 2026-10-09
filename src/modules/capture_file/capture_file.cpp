#include "augusta/capture_file.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <ios>
#include <istream>
#include <ostream>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/capture_error.h"
#include "augusta/protocol.h"

namespace augusta::capture_file {

namespace {

// A frame's length goes before it, least significant byte first, in at most this many bytes.
constexpr std::size_t kMaxLengthBytes = 4;
constexpr int kBitsPerByte = 8;
constexpr std::size_t kByteMask = 0xFFU;

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
  const Frame frame = ReadFrame(in, kCaptureFrames, payload);
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

void WriteFrame(std::ostream& out, const FrameFormat& format, std::span<const std::byte> payload) {
  std::array<char, kMaxLengthBytes> length{};
  for (std::size_t i = 0; i < format.length_bytes; ++i) {
    length[i] = static_cast<char>((payload.size() >> (kBitsPerByte * i)) & kByteMask);
  }
  out.write(length.data(), static_cast<std::streamsize>(format.length_bytes));
  out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
}

Frame ReadFrame(std::istream& in, const FrameFormat& format, std::vector<std::byte>& payload) {
  std::array<char, kMaxLengthBytes> length_bytes{};
  in.read(length_bytes.data(), static_cast<std::streamsize>(format.length_bytes));
  if (in.bad()) {
    return Frame::kUnreadable;
  }
  const auto length_read = static_cast<std::size_t>(in.gcount());
  if (length_read == 0) {
    return Frame::kEnd;
  }
  if (length_read < format.length_bytes) {
    return Frame::kTorn;
  }
  std::size_t length = 0;
  for (std::size_t i = 0; i < format.length_bytes; ++i) {
    length |= static_cast<std::size_t>(static_cast<unsigned char>(length_bytes[i])) << (kBitsPerByte * i);
  }
  if (length > format.max_payload) {
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
  for (Frame frame = ReadFrame(in, kCaptureFrames, payload); frame != Frame::kEnd;
       frame = ReadFrame(in, kCaptureFrames, payload)) {
    if (frame == Frame::kUnreadable) {
      return std::unexpected(ReadError::kUnreadable);
    }
    if (frame == Frame::kTorn) {
      file.torn = true;
      break;
    }
    if (frame == Frame::kTooLong) {
      return std::unexpected(ReadError::kMalformed);
    }
    auto record = protocol::DecodeCaptureRecord(payload);
    if (!record.has_value() || !order.Admits(*record)) {
      return std::unexpected(ReadError::kMalformed);
    }
    file.records.push_back(*std::move(record));
  }
  return file;
}

}  // namespace augusta::capture_file
