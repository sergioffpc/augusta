#ifndef AUGUSTA_TESTS_PROTOBUF_BYTES_H_
#define AUGUSTA_TESTS_PROTOBUF_BYTES_H_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "augusta/protocol.h"

// Payloads built by hand in protobuf's wire format (ADR-0007), field by field
// as proto/augusta.proto numbers them, so a test case says exactly what its
// payload holds without the generated code.
namespace augusta::protobuf_bytes {

using protocol::BytesWire;

// Each of parts after the other.
inline BytesWire Cat(std::initializer_list<BytesWire> parts) {
  BytesWire bytes;
  for (const BytesWire& part : parts) {
    bytes.insert(bytes.end(), part.begin(), part.end());
  }
  return bytes;
}

// part, times times over.
inline BytesWire Repeat(const BytesWire& part, std::size_t times) {
  BytesWire bytes;
  for (std::size_t i = 0; i < times; ++i) {
    bytes.insert(bytes.end(), part.begin(), part.end());
  }
  return bytes;
}

// value in seven bits a byte, least significant first, the high bit set on
// every byte but the last.
inline BytesWire Varint(std::uint64_t value) {
  BytesWire bytes;
  do {
    auto byte = static_cast<std::uint8_t>(value & 0x7FU);
    value >>= 7U;
    if (value != 0) {
      byte |= 0x80U;
    }
    bytes.push_back(static_cast<std::byte>(byte));
  } while (value != 0);
  return bytes;
}

inline constexpr std::uint64_t kVarintType = 0;
inline constexpr std::uint64_t kLengthDelimitedType = 2;

inline BytesWire Tag(int field, std::uint64_t wire_type) {
  return Varint((static_cast<std::uint64_t>(field) << 3U) | wire_type);
}

// An unsigned integer, an enumeration or a bitfield.
inline BytesWire VarintField(int field, std::uint64_t value) { return Cat({Tag(field, kVarintType), Varint(value)}); }

// A signed grid count, a sint32: zigzag-encoded, so a small negative count is
// a small varint.
inline BytesWire SignedField(int field, std::int32_t value) {
  const auto bits = static_cast<std::uint32_t>(value);
  return VarintField(field, (bits << 1U) ^ (value < 0 ? 0xFFFFFFFFU : 0U));
}

// A string, bytes or a message, after its length.
inline BytesWire LengthField(int field, const BytesWire& contents) {
  return Cat({Tag(field, kLengthDelimitedType), Varint(contents.size()), contents});
}

inline BytesWire TextField(int field, std::string_view text) {
  BytesWire contents;
  for (const char letter : text) {
    contents.push_back(static_cast<std::byte>(letter));
  }
  return LengthField(field, contents);
}

}  // namespace augusta::protobuf_bytes

#endif  // AUGUSTA_TESTS_PROTOBUF_BYTES_H_
