#include <cstddef>
#include <cstdint>
#include <ios>
#include <sstream>
#include <string>

#include "augusta/capture.h"

// The fuzz target for reading a Match capture (ADR-0050, NFR-12): a file that
// may have been cut short, corrupted or written by anything at all, which
// capture::ReadCapture, the reader Replay, the Replay catalog and a Captured
// player share, must read or refuse without crashing.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::istringstream file(std::string(reinterpret_cast<const char*>(data), size), std::ios::binary);
  static_cast<void>(augusta::capture::ReadCapture(file));
  return 0;
}
