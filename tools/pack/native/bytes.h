#ifndef AUGUSTA_PACK_BYTES_H_
#define AUGUSTA_PACK_BYTES_H_

#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <span>
#include <string_view>
#include <vector>

#include <pybind11/pybind11.h>

/// \file
/// How the cooker's binding of augusta_assets (pack_encoder.h, pack_reader.h)
/// passes bytes across: blobs, hashes, keys and signatures are Python bytes on
/// one side and std::byte buffers on the other.
namespace augusta::pack {

inline std::vector<std::byte> BytesOf(std::string_view bytes) {
  const auto* first = reinterpret_cast<const std::byte*>(bytes.data());
  return {first, first + bytes.size()};
}

inline pybind11::bytes ToBytes(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// bytes as a fixed-size array, a hash or a key; raises ValueError, naming the
/// argument name, if it is not Size bytes long.
template <std::size_t Size>
std::array<std::byte, Size> FixedBytesOf(std::string_view bytes, std::string_view name) {
  if (bytes.size() != Size) {
    throw pybind11::value_error(std::format("{} must be {} bytes, got {}", name, Size, bytes.size()));
  }
  std::array<std::byte, Size> fixed{};
  std::memcpy(fixed.data(), bytes.data(), Size);
  return fixed;
}

}  // namespace augusta::pack

#endif  // AUGUSTA_PACK_BYTES_H_
