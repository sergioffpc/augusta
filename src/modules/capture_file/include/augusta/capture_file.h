#ifndef AUGUSTA_CAPTURE_FILE_H_
#define AUGUSTA_CAPTURE_FILE_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <istream>
#include <ostream>
#include <span>
#include <vector>

#include "augusta/capture_error.h"
#include "augusta/protocol.h"

/// \file
/// A Match capture's file format (ADR-0050), in one place for every side that
/// reads or writes one: the server's Capturer and ReadCapture
/// (src/server/capture.h) and a Captured player's harness::ReadScript. A file
/// is the capture's magic, then each record's payload after its length - in
/// one byte, as kCaptureFrames frames it - the header first. ReadCaptureFile
/// checks the magic, the header's format version and the records' order (every
/// Join first, numbered from 1, then the events in offset order, each naming a
/// player a Join did, nothing after the Match end), and hands the records on
/// in the protocol's own types: what they mean is each reader's to convert.
/// Pure stream I/O, on whichever thread holds the stream.
namespace augusta::capture_file {

/// How one kind of file frames its records: each record's payload after its
/// length, little-endian.
struct FrameFormat {
  /// How many bytes a payload's length takes, 1 to 4.
  std::size_t length_bytes = 0;
  /// The longest payload a frame holds, in bytes: a length past it is a
  /// corrupted one, refused before anything is allocated for it, and a payload
  /// past it is never written.
  std::size_t max_payload = 0;
};

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

/// A capture file's records, as the protocol decodes them.
struct CaptureFile {
  protocol::CaptureHeaderWire header;
  /// Every record after the header, in the order written.
  std::vector<protocol::CaptureRecordWire> records;
  /// Whether the file ended partway through a record, which was dropped: its
  /// writer stopped while writing it.
  bool torn = false;
};

/// Reads the capture in. A last record cut short is dropped and reported in
/// CaptureFile::torn; a stream that fails, wherever it does, is kUnreadable,
/// never a whole or torn capture.
[[nodiscard]] std::expected<CaptureFile, ReadError> ReadCaptureFile(std::istream& in);

}  // namespace augusta::capture_file

#endif  // AUGUSTA_CAPTURE_FILE_H_
