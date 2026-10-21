#ifndef AUGUSTA_UUID_H_
#define AUGUSTA_UUID_H_

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

/// \file
/// The one value type the globally unique IDs are: the Run ID, the Session ID
/// and the Match ID (GLOSSARY.md), each a UUIDv7 (RFC 9562) that Boost.Uuid's
/// time_generator_v7 takes (ADR-0035). Time-ordered, so IDs taken later sort
/// later, as bytes and as strings alike. Boost.Uuid stays behind uuid.cpp.
namespace augusta::uuid {

class Uuid {
 public:
  /// The 16 bytes an ID is.
  static constexpr std::size_t kByteCount = 16;
  using ByteArray = std::array<std::uint8_t, kByteCount>;

  /// The nil UUID, all zeros: no ID that Generate takes.
  constexpr Uuid() = default;

  /// The ID whose 16 bytes, in RFC 9562's (network) order, are bytes.
  static constexpr Uuid FromBytes(const ByteArray& bytes) { return Uuid(bytes); }

  /// Its 16 bytes in RFC 9562's order: what the wire and a file carry.
  [[nodiscard]] constexpr const ByteArray& Bytes() const { return bytes_; }

  /// The canonical lowercase hyphenated form, 36 characters, e.g.
  /// "017f22e2-79b0-7cc3-98c4-dc0c0c07398f": how an ID is written wherever it
  /// is logged or names a file.
  [[nodiscard]] std::string ToString() const;

  /// Byte by byte, so a UUIDv7 taken later compares greater.
  constexpr auto operator<=>(const Uuid&) const = default;

 private:
  constexpr explicit Uuid(const ByteArray& bytes) : bytes_(bytes) {}

  ByteArray bytes_{};
};

/// The nil UUID, Uuid{}: what an ID field holds until one is set, named so a
/// field can default to it and still be left out of a designated initializer.
inline constexpr Uuid kNil{};

/// A new UUIDv7, from one process-wide generator: each ID taken compares
/// greater than every one this process took before it, from any thread.
Uuid Generate();

/// The ID text writes in the canonical form (Uuid::ToString), and nothing else:
/// upper case, braces or a missing hyphen are nullopt.
std::optional<Uuid> Parse(std::string_view text);

/// What std::hash<Uuid> gives: every byte reaches it.
std::size_t Hash(const Uuid& id) noexcept;

}  // namespace augusta::uuid

template <>
struct std::hash<augusta::uuid::Uuid> {
  std::size_t operator()(const augusta::uuid::Uuid& id) const noexcept { return augusta::uuid::Hash(id); }
};

/// Formats as Uuid::ToString, so a line can write `run={}` with the ID itself.
template <>
struct std::formatter<augusta::uuid::Uuid> : std::formatter<std::string_view> {
  auto format(const augusta::uuid::Uuid& id, std::format_context& context) const {
    return std::formatter<std::string_view>::format(id.ToString(), context);
  }
};

#endif  // AUGUSTA_UUID_H_
