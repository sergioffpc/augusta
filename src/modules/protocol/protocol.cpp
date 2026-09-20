#include "augusta/protocol.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace augusta::protocol {

namespace {

constexpr int kBitsPerByte = 8;

void WriteU8(Bytes& out, std::uint8_t value) { out.push_back(static_cast<std::byte>(value)); }

void WriteU32(Bytes& out, std::uint32_t value) {
  for (int shift = 0; shift < std::numeric_limits<std::uint32_t>::digits; shift += kBitsPerByte) {
    WriteU8(out, static_cast<std::uint8_t>(value >> shift));
  }
}

// Walks a payload front to back; every read reports whether the bytes were
// there instead of running past the end.
class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  std::optional<std::uint8_t> ReadU8() {
    if (bytes_.empty()) {
      return std::nullopt;
    }
    const auto value = static_cast<std::uint8_t>(bytes_.front());
    bytes_ = bytes_.subspan(1);
    return value;
  }

  std::optional<std::uint32_t> ReadU32() {
    constexpr std::size_t kSize = sizeof(std::uint32_t);
    if (bytes_.size() < kSize) {
      return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < kSize; ++i) {
      value |= static_cast<std::uint32_t>(bytes_[i]) << (kBitsPerByte * i);
    }
    bytes_ = bytes_.subspan(kSize);
    return value;
  }

  // The length is checked against what is left before anything is allocated.
  std::optional<std::string> ReadBytes(std::size_t length) {
    if (bytes_.size() < length) {
      return std::nullopt;
    }
    std::string value(length, '\0');
    std::transform(bytes_.begin(), bytes_.begin() + static_cast<std::ptrdiff_t>(length), value.begin(),
                   [](std::byte byte) { return static_cast<char>(byte); });
    bytes_ = bytes_.subspan(length);
    return value;
  }

  [[nodiscard]] bool AtEnd() const { return bytes_.empty(); }

 private:
  std::span<const std::byte> bytes_;
};

std::expected<Message, DecodeError> DecodeJoinRequest(Reader& reader) {
  const std::optional<std::uint8_t> length = reader.ReadU8();
  if (!length.has_value()) {
    return std::unexpected(DecodeError::kTruncated);
  }
  if (*length > kMaxEngineVersionLength) {
    return std::unexpected(DecodeError::kFieldTooLong);
  }
  std::optional<std::string> version = reader.ReadBytes(*length);
  if (!version.has_value()) {
    return std::unexpected(DecodeError::kTruncated);
  }
  return JoinRequest{.engine_version = std::move(*version)};
}

std::expected<Message, DecodeError> DecodeJoinAccepted(Reader& reader) {
  const std::optional<std::uint32_t> session = reader.ReadU32();
  if (!session.has_value()) {
    return std::unexpected(DecodeError::kTruncated);
  }
  return JoinAccepted{.session = static_cast<SessionId>(*session)};
}

std::expected<Message, DecodeError> DecodeJoinRefused(Reader& reader) {
  const std::optional<std::uint8_t> reason = reader.ReadU8();
  if (!reason.has_value()) {
    return std::unexpected(DecodeError::kTruncated);
  }
  switch (static_cast<JoinRefusal>(*reason)) {
    case JoinRefusal::kVersionMismatch:
    case JoinRefusal::kMatchFull:
      return JoinRefused{.reason = static_cast<JoinRefusal>(*reason)};
  }
  return std::unexpected(DecodeError::kInvalidEnum);
}

std::expected<Message, DecodeError> DecodeBody(MessageType type, Reader& reader) {
  switch (type) {
    case MessageType::kJoinRequest:
      return DecodeJoinRequest(reader);
    case MessageType::kJoinAccepted:
      return DecodeJoinAccepted(reader);
    case MessageType::kJoinRefused:
      return DecodeJoinRefused(reader);
  }
  return std::unexpected(DecodeError::kUnknownType);
}

// One overload per message: the type tag, then the fields.
struct Encoder {
  Bytes& out;

  void operator()(const JoinRequest& message) const {
    assert(message.engine_version.size() <= kMaxEngineVersionLength);
    WriteU8(out, static_cast<std::uint8_t>(MessageType::kJoinRequest));
    WriteU8(out, static_cast<std::uint8_t>(message.engine_version.size()));
    for (const char character : message.engine_version) {
      out.push_back(static_cast<std::byte>(character));
    }
  }

  void operator()(const JoinAccepted& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageType::kJoinAccepted));
    WriteU32(out, static_cast<std::uint32_t>(message.session));
  }

  void operator()(const JoinRefused& message) const {
    WriteU8(out, static_cast<std::uint8_t>(MessageType::kJoinRefused));
    WriteU8(out, static_cast<std::uint8_t>(message.reason));
  }
};

}  // namespace

Bytes Encode(const Message& message) {
  Bytes out;
  std::visit(Encoder{.out = out}, message);
  return out;
}

std::expected<Message, DecodeError> Decode(std::span<const std::byte> payload) {
  Reader reader(payload);
  const std::optional<std::uint8_t> type = reader.ReadU8();
  if (!type.has_value()) {
    return std::unexpected(DecodeError::kEmpty);
  }
  std::expected<Message, DecodeError> message = DecodeBody(static_cast<MessageType>(*type), reader);
  if (message.has_value() && !reader.AtEnd()) {
    return std::unexpected(DecodeError::kTrailingBytes);
  }
  return message;
}

std::string_view DescribeDecodeError(DecodeError error) {
  switch (error) {
    case DecodeError::kEmpty:
      return "empty payload";
    case DecodeError::kUnknownType:
      return "unknown message type";
    case DecodeError::kTruncated:
      return "payload ends before the message does";
    case DecodeError::kTrailingBytes:
      return "bytes remain after the message";
    case DecodeError::kInvalidEnum:
      return "field holds a value its enumeration lacks";
    case DecodeError::kFieldTooLong:
      return "string field longer than allowed";
  }
  return "unknown decode error";
}

std::string_view DescribeJoinRefusal(JoinRefusal reason) {
  switch (reason) {
    case JoinRefusal::kVersionMismatch:
      return "client version does not match the server";
    case JoinRefusal::kMatchFull:
      return "match is full";
  }
  return "unknown refusal";
}

}  // namespace augusta::protocol
