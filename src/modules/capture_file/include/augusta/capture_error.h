#ifndef AUGUSTA_CAPTURE_ERROR_H_
#define AUGUSTA_CAPTURE_ERROR_H_

#include <cstdint>
#include <string_view>

/// \file
/// Why a Match capture file (ADR-0050) does not read, wherever it is read:
/// the server's ReadCapture and a Captured player's harness::ReadScript both
/// read it through augusta/capture_file.h and report the same errors. Apart
/// from that header so a reader's own header names them without the protocol.
namespace augusta::capture_file {

enum class ReadError : std::uint8_t {
  /// The stream could not be read, or was never opened.
  kUnreadable,
  /// It does not start with a capture's magic.
  kNotACapture,
  /// Its header is missing, or of a format version this engine does not read.
  kUnsupported,
  /// A record does not decode, comes out of order, or names a player no Join did.
  kMalformed,
};

/// What to tell whoever runs the process about error.
[[nodiscard]] std::string_view DescribeReadError(ReadError error);

}  // namespace augusta::capture_file

#endif  // AUGUSTA_CAPTURE_ERROR_H_
