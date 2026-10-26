#ifndef AUGUSTA_PROTOCOL_FRAMING_H_
#define AUGUSTA_PROTOCOL_FRAMING_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "augusta.pb.h"
#include "generated_messages.h"

/// \file
/// Readdress's framing (ADR-0038): protobuf's own tags and varints, around the
/// generated messages, so a tick's bodies are serialized once; and the envelope
/// tag a payload's type is read from without decoding it. Private to the
/// module.
namespace augusta::protocol {

/// The tag before a field: its number, shifted past three bits of wire type.
constexpr unsigned kTagTypeBits = 3U;
constexpr std::uint32_t kLengthDelimitedType = 2;
/// A tag is a varint of at most 32 bits: five bytes of seven bits each.
constexpr std::size_t kMaxTagBytes = 5;

constexpr std::uint32_t kAuthoritativeStateTag =
    (static_cast<std::uint32_t>(pb::Message::kAuthoritativeStateFieldNumber) << kTagTypeBits) | kLengthDelimitedType;

/// The bytes of payload's shared fields, if it is an Authoritative State as
/// Encode makes one: the envelope's member, as long as the rest of the payload,
/// with its fields in their numbers' order, so its shared fields come first and
/// end where its recipient's own begin.
std::optional<std::span<const std::byte>> SharedFieldsOf(std::span<const std::byte> payload);

}  // namespace augusta::protocol

#endif  // AUGUSTA_PROTOCOL_FRAMING_H_
