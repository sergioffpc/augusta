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

// The canonical form's length, and where its hyphens fall (RFC 9562, 4).
constexpr std::size_t kTextLength = 36;
constexpr std::array<std::size_t, 4> kHyphens{8, 13, 18, 23};

bool IsHyphenAt(std::size_t index) { return std::ranges::find(kHyphens, index) != kHyphens.end(); }

std::optional<std::uint8_t> HexValue(char digit) {
  if (digit >= '0' && digit <= '9') {
    return static_cast<std::uint8_t>(digit - '0');
  }
  if (digit >= 'a' && digit <= 'f') {
    return static_cast<std::uint8_t>(digit - 'a' + 10);
  }
  return std::nullopt;
}

}  // namespace

std::string Uuid::ToString() const {
  std::string text;
  text.reserve(kTextLength);
  for (std::size_t i = 0; i < bytes_.size(); ++i) {
    if (IsHyphenAt(text.size())) {
      text.push_back('-');
    }
    text.push_back(kHexDigits[bytes_[i] >> 4U]);
    text.push_back(kHexDigits[bytes_[i] & 0x0FU]);
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

}  // namespace augusta::uuid

std::size_t std::hash<augusta::uuid::Uuid>::operator()(const augusta::uuid::Uuid& id) const noexcept {
  // FNV-1a over the 16 bytes: a UUIDv7's leading bytes are its time, so every
  // byte has to reach the hash.
  std::uint64_t hash = 14695981039346656037ULL;
  for (const std::uint8_t byte : id.Bytes()) {
    hash = (hash ^ byte) * 1099511628211ULL;
  }
  return static_cast<std::size_t>(hash);
}
