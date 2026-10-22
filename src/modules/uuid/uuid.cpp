#include "augusta/uuid.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <boost/uuid/time_generator_v7.hpp>
#include <boost/uuid/uuid.hpp>

namespace augusta::uuid {

namespace {

constexpr std::string_view kHexDigits = "0123456789abcdef";

// Where the canonical form's hyphens fall (RFC 9562, 4).
constexpr std::array<std::size_t, 4> kHyphens{8, 13, 18, 23};

// A hex digit's value past 9 counts from 10, and a byte's low digit is its low
// four bits.
constexpr std::uint8_t kFirstLetterValue = 10;
constexpr unsigned kLowNibble = 0x0FU;

// FNV-1a's 64-bit offset basis and prime.
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

bool IsHyphenAt(std::size_t index) { return std::ranges::find(kHyphens, index) != kHyphens.end(); }

std::optional<std::uint8_t> HexValue(char digit) {
  if (digit >= '0' && digit <= '9') {
    return static_cast<std::uint8_t>(digit - '0');
  }
  if (digit >= 'a' && digit <= 'f') {
    return static_cast<std::uint8_t>(digit - 'a' + kFirstLetterValue);
  }
  return std::nullopt;
}

}  // namespace

std::string Uuid::ToString() const {
  std::string text;
  text.reserve(kTextLength);
  for (const std::uint8_t byte : bytes_) {
    if (IsHyphenAt(text.size())) {
      text.push_back('-');
    }
    text.push_back(kHexDigits[byte >> 4U]);
    text.push_back(kHexDigits[byte & kLowNibble]);
  }
  return text;
}

Uuid Generate() {
  // One generator for the process, under a lock: time_generator_v7 is not
  // thread-safe, and only one generator keeps every ID it takes in order.
  static std::mutex mutex;
  static boost::uuids::time_generator_v7 generator;
  boost::uuids::uuid taken;
  {
    const std::scoped_lock lock(mutex);
    taken = generator();
  }
  Uuid::ByteArray bytes{};
  std::ranges::copy(taken, bytes.begin());
  return Uuid::FromBytes(bytes);
}

std::optional<Uuid> Parse(std::string_view text) {
  if (text.size() != kTextLength) {
    return std::nullopt;
  }
  Uuid::ByteArray bytes{};
  std::size_t at = 0;
  for (std::uint8_t& byte : bytes) {
    if (IsHyphenAt(at)) {
      if (text[at] != '-') {
        return std::nullopt;
      }
      ++at;
    }
    const auto high = HexValue(text[at]);
    const auto low = HexValue(text[at + 1]);
    if (!high || !low) {
      return std::nullopt;
    }
    byte = static_cast<std::uint8_t>((*high << 4U) | *low);
    at += 2;
  }
  return Uuid::FromBytes(bytes);
}

std::size_t Hash(const Uuid& id) noexcept {
  // FNV-1a over the 16 bytes: a UUIDv7's leading bytes are its time, so every
  // byte has to reach the hash.
  std::uint64_t hash = kFnvOffsetBasis;
  for (const std::uint8_t byte : id.Bytes()) {
    hash = (hash ^ byte) * kFnvPrime;
  }
  return static_cast<std::size_t>(hash);
}

}  // namespace augusta::uuid
