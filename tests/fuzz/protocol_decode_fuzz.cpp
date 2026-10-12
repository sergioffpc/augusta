#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "augusta/protocol.h"

// The fuzz target for the protocol's message decoding (ADR-0013), the NFR-05
// attack surface: every payload a peer receives goes through Decode. Beyond not
// crashing, a payload Decode accepts must be the one Encode writes for what it
// decoded, since every field travels in exactly one way (a grid count, a float's
// bits, a validated enum or flag byte): a decoder that accepts a payload it
// misread fails here even when nothing crashes.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::byte> payload(reinterpret_cast<const std::byte*>(data), size);
  const auto message = augusta::protocol::Decode(payload);
  if (message.has_value() && !std::ranges::equal(augusta::protocol::Encode(*message).value(), payload)) {
    std::abort();
  }
  return 0;
}
