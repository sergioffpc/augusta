#ifndef AUGUSTA_SERVER_FRAMES_H_
#define AUGUSTA_SERVER_FRAMES_H_

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <span>
#include <vector>

/// \file
/// How a Match capture (capture.h, ADR-0050) holds its records one after
/// another: each record's payload after its length, in one byte; what a
/// payload holds is its own.
/// Pure stream I/O, on whichever thread holds the stream.
namespace augusta::server {

/// The longest payload a frame holds, in bytes: none of a capture's records,
/// header included, passes it, so one byte of length is enough (#463).
inline constexpr std::size_t kMaxFramePayload = 255;

/// Writes payload's frame to out, whose state then says whether it was.
/// payload is at most kMaxFramePayload bytes.
void WriteFrame(std::ostream& out, std::span<const std::byte> payload);

/// How reading the next frame ended.
enum class Frame : std::uint8_t {
  kRead,
  /// The stream ended where a frame would start.
  kEnd,
  /// The stream ended partway through a frame.
  kTorn,
  /// The stream failed: what it holds past here is unknown, so neither an end
  /// nor a torn frame can be told from it.
  kUnreadable,
};

/// Reads the next frame from in into payload.
[[nodiscard]] Frame ReadFrame(std::istream& in, std::vector<std::byte>& payload);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_FRAMES_H_
