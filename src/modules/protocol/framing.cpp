#include "framing.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include <google/protobuf/io/coded_stream.h>

#include "augusta.pb.h"

namespace augusta::protocol {

namespace {

constexpr std::uint32_t kTagTypeMask = (1U << kTagTypeBits) - 1U;
constexpr std::uint32_t kVarintType = 0;
constexpr std::uint32_t kFixed64Type = 1;
constexpr std::uint32_t kFixed32Type = 5;
constexpr int kFixed64Size = 8;
constexpr int kFixed32Size = 4;

// The last of an Authoritative State's shared fields, which number below every
// field that is its recipient's own (proto/augusta.proto).
constexpr std::uint32_t kLastSharedField = pb::AuthoritativeState::kBodiesFieldNumber;

// Steps input past the value of the field tag begins: false if it runs past
// the end, or is of a wire type an Authoritative State's fields never have.
bool SkipValue(google::protobuf::io::CodedInputStream& input, std::uint32_t tag) {
  std::uint64_t varint = 0;
  std::uint32_t length = 0;
  switch (tag & kTagTypeMask) {
    case kVarintType:
      return input.ReadVarint64(&varint);
    case kFixed64Type:
      return input.Skip(kFixed64Size);
    case kLengthDelimitedType:
      return input.ReadVarint32(&length) && length <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
             input.Skip(static_cast<int>(length));
    case kFixed32Type:
      return input.Skip(kFixed32Size);
    default:
      return false;
  }
}

}  // namespace

std::optional<std::span<const std::byte>> SharedFieldsOf(std::span<const std::byte> payload) {
  if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  google::protobuf::io::CodedInputStream input(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                               static_cast<int>(payload.size()));
  std::uint32_t length = 0;
  if (input.ReadTag() != kAuthoritativeStateTag || !input.ReadVarint32(&length)) {
    return std::nullopt;
  }
  const auto start = static_cast<std::size_t>(input.CurrentPosition());
  if (payload.size() - start != length) {
    return std::nullopt;
  }
  std::optional<std::size_t> own_start;
  while (static_cast<std::size_t>(input.CurrentPosition()) < payload.size()) {
    const auto at = static_cast<std::size_t>(input.CurrentPosition());
    const std::uint32_t tag = input.ReadTag();
    const std::uint32_t field = tag >> kTagTypeBits;
    if (field == 0 || (own_start.has_value() && field <= kLastSharedField) || !SkipValue(input, tag)) {
      return std::nullopt;
    }
    if (!own_start.has_value() && field > kLastSharedField) {
      own_start = at;
    }
  }
  return payload.subspan(start, own_start.value_or(payload.size()) - start);
}

}  // namespace augusta::protocol
