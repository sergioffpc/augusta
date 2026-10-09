#include "frames.h"

#include <array>
#include <cstddef>
#include <ios>
#include <istream>
#include <ostream>
#include <span>
#include <vector>

namespace augusta::server {

namespace {

// A frame's length goes before it, least significant byte first, in at most this many bytes.
constexpr std::size_t kMaxLengthBytes = 4;
constexpr int kBitsPerByte = 8;
constexpr std::size_t kByteMask = 0xFFU;

}  // namespace

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

}  // namespace augusta::server
