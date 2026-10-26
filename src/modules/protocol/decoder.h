#ifndef AUGUSTA_PROTOCOL_DECODER_H_
#define AUGUSTA_PROTOCOL_DECODER_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/uuid/uuid.hpp>

#include "augusta.pb.h"
#include "augusta/grid.h"
#include "augusta/math.h"
#include "augusta/protocol.h"
#include "generated_messages.h"

/// \file
/// The decoder's half of the codec: how Decode maps a parsed augusta.Message
/// to its *Wire, refusing any value no valid message of this build holds. A
/// Match capture's records (capture_records.h) read their fields with the same
/// pieces. Private to the module (ADR-0007, ADR-0038).
namespace augusta::protocol {

/// What Decode maps a parsed message to its *Wire with. The first value it meets
/// that no valid message of this build holds is remembered, and every check
/// after gives a zero value, so a mapping reads all of a message's fields and
/// the caller asks once at the end (ADR-0038, Untrusted input).
class Inbound {
 public:
  /// count of grid's step as the value it stands for. A count outside the
  /// grid's range is none a sender, clamping, ever writes.
  float FromSteps(std::int64_t count, const math::Grid& grid) {
    if (count < grid.min || count > grid.max) {
      Fail(DecodeError::kOutOfRange);
      return 0.0F;
    }
    return math::FromSteps(static_cast<std::int32_t>(count), grid);
  }

  math::Vec3 FromSteps(std::int64_t x, std::int64_t y, std::int64_t z, const math::Grid& grid) {
    return {FromSteps(x, grid), FromSteps(y, grid), FromSteps(z, grid)};
  }

  /// value, which its varint carries wider than its *Wire field, Field: one
  /// byte unless said otherwise.
  template <typename Field = std::uint8_t>
  Field Narrow(std::uint32_t value) {
    if (value > std::numeric_limits<Field>::max()) {
      Fail(DecodeError::kOutOfRange);
      return 0;
    }
    return static_cast<Field>(value);
  }

  /// value as an enumerator between first and last, which must be consecutive.
  template <typename Enum>
  Enum ToEnum(std::int64_t value, Enum first, Enum last) {
    if (value < static_cast<std::int64_t>(first) || value > static_cast<std::int64_t>(last)) {
      Fail(DecodeError::kInvalidEnum);
      return first;
    }
    return static_cast<Enum>(value);
  }

  /// bits, of which only those of mask may be set.
  std::uint32_t ToBits(std::uint32_t bits, std::uint32_t mask) {
    if ((bits & ~mask) != 0) {
      Fail(DecodeError::kReservedBits);
      return 0;
    }
    return bits;
  }

  /// text, which may be at most max_length bytes long, and must be UTF-8: the
  /// schema holds it as bytes, so protobuf's parse never checks it, and never
  /// logs a line a peer's bad text would make at will (NFR-05).
  std::string Text(std::string_view text, std::size_t max_length) {
    if (text.size() > max_length) {
      Fail(DecodeError::kFieldTooLong);
      return {};
    }
    if (!IsUtf8(text)) {
      Fail(DecodeError::kMalformed);
      return {};
    }
    return std::string(text);
  }

  /// bytes as a pack hash: none is the all-zero one an absent field reads as,
  /// and any other length than a hash's is a number wider than its field.
  PackHashWire PackHash(std::string_view bytes) {
    PackHashWire hash{};
    if (bytes.empty()) {
      return hash;
    }
    if (bytes.size() != hash.size()) {
      Fail(DecodeError::kOutOfRange);
      return hash;
    }
    std::ranges::transform(bytes, hash.begin(), [](char byte) { return static_cast<std::byte>(byte); });
    return hash;
  }

  /// bytes as a UUID (a Run ID, a Match ID): none is the nil one an absent field reads as,
  /// and any other length than a UUID's is a number wider than its field.
  boost::uuids::uuid Uuid(std::string_view bytes) {
    boost::uuids::uuid id{};
    if (bytes.empty()) {
      return id;
    }
    if (bytes.size() != id.size()) {
      Fail(DecodeError::kOutOfRange);
      return id;
    }
    std::ranges::transform(bytes, id.begin(), [](char byte) { return static_cast<std::uint8_t>(byte); });
    return id;
  }

  /// Whether a list of size elements is at most max long.
  bool Fits(int size, std::size_t max) {
    if (std::cmp_greater(size, max)) {
      Fail(DecodeError::kFieldTooLong);
      return false;
    }
    return true;
  }

  [[nodiscard]] std::optional<DecodeError> Error() const { return error_; }

 private:
  void Fail(DecodeError error) {
    if (!error_.has_value()) {
      error_ = error;
    }
  }

  std::optional<DecodeError> error_;
};

CommandWire Read(const pb::Command& command, Inbound& in);

/// message's body, or nullopt when it sets no member this build knows.
std::optional<MessageWire> ReadBody(const pb::Message& message, Inbound& in);

}  // namespace augusta::protocol

#endif  // AUGUSTA_PROTOCOL_DECODER_H_
