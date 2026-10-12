#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ios>
#include <span>
#include <sstream>
#include <string>

#include "augusta/protocol.h"
#include "capture.h"

// The fuzz target for reading a Match capture (ADR-0050, NFR-12): a file that
// may have been cut short, corrupted or written by anything at all. The input
// is read twice: as one record's payload, which DecodeCaptureRecord must
// accept only as the very bytes EncodeCaptureRecord writes for what it decoded
// (as protocol_decode holds Decode to Encode), and as a whole file, which
// ReadCapture must read or refuse without crashing.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::byte> payload(reinterpret_cast<const std::byte*>(data), size);
  const auto record = augusta::protocol::DecodeCaptureRecord(payload);
  if (record.has_value() && !std::ranges::equal(augusta::protocol::EncodeCaptureRecord(*record).value(), payload)) {
    std::abort();
  }
  std::istringstream file(std::string(reinterpret_cast<const char*>(data), size), std::ios::binary);
  static_cast<void>(augusta::server::ReadCapture(file));
  return 0;
}
