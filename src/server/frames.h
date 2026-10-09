#ifndef AUGUSTA_SERVER_FRAMES_H_
#define AUGUSTA_SERVER_FRAMES_H_

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <span>
#include <vector>

/// \file
/// How the server's files hold records one after another: each record's
/// payload after its length, little-endian, in as many bytes as its file's
/// FrameFormat says. A Match recording (recording.h, ADR-0048) takes 4 bytes,
/// a Match capture (capture.h, ADR-0050) one; what a payload holds is theirs.
/// Pure stream I/O, on whichever thread holds the stream.
namespace augusta::server {

/// How one kind of file frames its records.
struct FrameFormat {
  /// How many bytes a payload's length takes, 1 to 4.
  std::size_t length_bytes = 0;
  /// The longest payload a frame holds, in bytes: a length past it is a
  /// corrupted one, refused before anything is allocated for it, and a payload
  /// past it is never written.
  std::size_t max_payload = 0;
};

/// A Match recording's frames: its records hold whole Authoritative States.
inline constexpr FrameFormat kRecordingFrames{.length_bytes = 4, .max_payload = std::size_t{64} * 1024};

/// A Match capture's frames: none of its records, header included, passes 255
/// bytes, so one byte of length is enough (#463).
inline constexpr FrameFormat kCaptureFrames{.length_bytes = 1, .max_payload = 255};

/// How many bytes a frame of a payload this long takes in a file of format.
[[nodiscard]] constexpr std::size_t FrameSize(const FrameFormat& format, std::size_t payload_size) {
  return format.length_bytes + payload_size;
}

/// Writes payload's frame to out in format, whose state then says whether it
/// was. payload is at most format.max_payload bytes.
void WriteFrame(std::ostream& out, const FrameFormat& format, std::span<const std::byte> payload);

/// How reading the next frame ended.
enum class Frame : std::uint8_t {
  kRead,
  /// The stream ended where a frame would start.
  kEnd,
  /// The stream ended partway through a frame.
  kTorn,
  /// Its length is more than its format's max_payload.
  kTooLong,
  /// The stream failed: what it holds past here is unknown, so neither an end
  /// nor a torn frame can be told from it.
  kUnreadable,
};

/// Reads the next frame of format from in into payload.
[[nodiscard]] Frame ReadFrame(std::istream& in, const FrameFormat& format, std::vector<std::byte>& payload);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_FRAMES_H_
