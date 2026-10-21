#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "augusta/protocol.h"

// The fuzz target for the protocol's decoding (ADR-0013), the NFR-05 attack
// surface: every payload a peer receives goes through Decode, and every record
// of a Match capture (ADR-0050) through DecodeCaptureRecord. Beyond not
// crashing, whatever either accepts must be something its encoder carries (an
// EncodeError aborts), and must survive encoding and decoding again as the
// same value: a decoder that accepts more than the protocol carries, or reads
// a field into the wrong place, fails here even when nothing crashes. One
// message has many encodings (ADR-0007), so the payload itself need not come
// back. The value is compared through its encoding, which keeps a float bit for
// bit, since a NaN a payload may carry never equals itself.
namespace {

template <typename Value, typename Encoder, typename Decoder>
void ExpectRoundTrip(const Value& value, Encoder encode, Decoder decode) {
  const auto encoded = encode(value);
  if (!encoded.has_value()) {
    std::abort();
  }
  const auto decoded = decode(std::span<const std::byte>(*encoded));
  if (!decoded.has_value() || encode(*decoded) != encoded) {
    std::abort();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::byte> payload(reinterpret_cast<const std::byte*>(data), size);
  if (const auto message = augusta::protocol::Decode(payload); message.has_value()) {
    ExpectRoundTrip(*message, augusta::protocol::Encode, augusta::protocol::Decode);
  }
  if (const auto record = augusta::protocol::DecodeCaptureRecord(payload); record.has_value()) {
    ExpectRoundTrip(*record, augusta::protocol::EncodeCaptureRecord, augusta::protocol::DecodeCaptureRecord);
  }
  return 0;
}
