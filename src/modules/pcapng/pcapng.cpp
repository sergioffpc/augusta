#include "pcapng.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/networking.h"

namespace augusta::pcapng {
namespace {

constexpr std::uint32_t kSectionHeaderBlock = 0x0A0D0D0A;
constexpr std::uint32_t kByteOrderMagic = 0x1A2B3C4D;
constexpr std::uint16_t kMajorVersion = 1;
constexpr std::uint16_t kMinorVersion = 0;
constexpr std::uint64_t kUnspecifiedSectionLength = 0xFFFFFFFFFFFFFFFF;
constexpr std::uint16_t kOptEndOfOpt = 0;
constexpr std::uint16_t kShbUserappl = 4;
constexpr std::uint32_t kInterfaceDescriptionBlock = 0x00000001;
constexpr std::uint16_t kLinkTypeWiresharkUpperPdu = 252;
constexpr std::uint32_t kNoSnapLength = 0;
constexpr std::uint16_t kIfTsresol = 9;
// if_tsresol's 10^-6: a timestamp counts microseconds.
constexpr std::uint8_t kMicrosecondsExponent = 6;
constexpr std::uint32_t kEnhancedPacketBlock = 0x00000006;
constexpr std::uint32_t kInterfaceId = 0;
constexpr std::uint16_t kEpbFlags = 2;
// epb_flags' inbound/outbound bits, 0-1.
constexpr std::uint32_t kEpbFlagsInbound = 1;
constexpr std::uint32_t kEpbFlagsOutbound = 2;
// Wireshark's exported_pdu tags (wsutil/exported_pdu_tlvs.h).
constexpr std::uint16_t kTagEndOfOpt = 0;
constexpr std::uint16_t kTagDissectorName = 12;
constexpr std::uint16_t kTagIpv4Source = 20;
constexpr std::uint16_t kTagIpv4Destination = 21;
constexpr std::uint16_t kTagIpv6Source = 22;
constexpr std::uint16_t kTagIpv6Destination = 23;
constexpr std::uint16_t kTagSourcePort = 25;
constexpr std::uint16_t kTagDestinationPort = 26;
constexpr std::size_t kPortValueBytes = 4;
constexpr unsigned kTimestampHalfBits = 32;
// The dissector tools/wireshark/augusta.lua registers.
constexpr std::string_view kDissectorName = "augusta";
constexpr std::size_t kAlignment = 4;
// A block's type and total length before its body, and its total length again after.
constexpr std::size_t kBlockFraming = 12;

constexpr unsigned kBitsPerByte = 8;
constexpr unsigned kByteMask = 0xFF;

// value's low bytes, least significant first: little-endian, the section's
// byte order.
void AppendLittleEndian(std::vector<std::byte>& out, std::uint64_t value, std::size_t bytes) {
  for (std::size_t i = 0; i < bytes; ++i) {
    out.push_back(static_cast<std::byte>((value >> (kBitsPerByte * i)) & kByteMask));
  }
}

// value's low bytes, most significant first: big-endian, exported_pdu's order.
void AppendBigEndian(std::vector<std::byte>& out, std::uint64_t value, std::size_t bytes) {
  for (std::size_t i = bytes; i > 0; --i) {
    out.push_back(static_cast<std::byte>((value >> (kBitsPerByte * (i - 1))) & kByteMask));
  }
}

void AppendU16(std::vector<std::byte>& out, std::uint16_t value) { AppendLittleEndian(out, value, sizeof value); }
void AppendU32(std::vector<std::byte>& out, std::uint32_t value) { AppendLittleEndian(out, value, sizeof value); }
void AppendU64(std::vector<std::byte>& out, std::uint64_t value) { AppendLittleEndian(out, value, sizeof value); }

void PadToAlignment(std::vector<std::byte>& out) {
  while (out.size() % kAlignment != 0) {
    out.push_back(std::byte{0});
  }
}

void AppendOption(std::vector<std::byte>& out, std::uint16_t code, std::span<const std::byte> value) {
  AppendU16(out, code);
  AppendU16(out, static_cast<std::uint16_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
  PadToAlignment(out);
}

std::span<const std::byte> AsBytes(std::string_view text) { return std::as_bytes(std::span(text)); }

// exported_pdu's TLVs are big-endian, whatever the section's byte order.
void AppendTlv(std::vector<std::byte>& out, std::uint16_t tag, std::span<const std::byte> value) {
  const auto length = static_cast<std::uint16_t>((value.size() + kAlignment - 1) / kAlignment * kAlignment);
  AppendBigEndian(out, tag, sizeof tag);
  AppendBigEndian(out, length, sizeof length);
  out.insert(out.end(), value.begin(), value.end());
  PadToAlignment(out);
}

// A port as exported_pdu's 4-byte value, whatever the protocol's port size.
std::vector<std::byte> PortValue(std::uint16_t port) {
  std::vector<std::byte> value;
  AppendBigEndian(value, port, kPortValueBytes);
  return value;
}

// The TLVs in front of a packet's payload, in tag order, as Wireshark asks:
// the dissector, the addresses, the ports, the end.
std::vector<std::byte> ExportedPduTlvs(const networking::TapEndpoint& source,
                                       const networking::TapEndpoint& destination) {
  std::vector<std::byte> tlvs;
  AppendTlv(tlvs, kTagDissectorName, AsBytes(kDissectorName));
  // Each address under its own family's tag.
  const auto append_address = [&](const networking::TapEndpoint& endpoint, std::uint16_t ipv4_tag,
                                  std::uint16_t ipv6_tag) {
    const bool ipv6 = std::holds_alternative<networking::Ipv6Address>(endpoint.address);
    std::visit(
        [&](const auto& address) { AppendTlv(tlvs, ipv6 ? ipv6_tag : ipv4_tag, std::as_bytes(std::span(address))); },
        endpoint.address);
  };
  append_address(source, kTagIpv4Source, kTagIpv6Source);
  append_address(destination, kTagIpv4Destination, kTagIpv6Destination);
  AppendTlv(tlvs, kTagSourcePort, PortValue(source.port));
  AppendTlv(tlvs, kTagDestinationPort, PortValue(destination.port));
  AppendTlv(tlvs, kTagEndOfOpt, {});
  return tlvs;
}

// A block of type around body, which ends in its options: the end of options,
// and the block's total length before and after it.
std::vector<std::byte> Block(std::uint32_t type, std::vector<std::byte> body) {
  AppendU16(body, kOptEndOfOpt);
  AppendU16(body, 0);
  const auto total = static_cast<std::uint32_t>(body.size() + kBlockFraming);
  std::vector<std::byte> block;
  block.reserve(total);
  AppendU32(block, type);
  AppendU32(block, total);
  block.insert(block.end(), body.begin(), body.end());
  AppendU32(block, total);
  return block;
}

}  // namespace

std::vector<std::byte> EncodeSectionHeader(std::string_view executable, std::string_view version) {
  std::vector<std::byte> body;
  AppendU32(body, kByteOrderMagic);
  AppendU16(body, kMajorVersion);
  AppendU16(body, kMinorVersion);
  AppendU64(body, kUnspecifiedSectionLength);
  AppendOption(body, kShbUserappl, AsBytes(std::format("{} {}", executable, version)));
  return Block(kSectionHeaderBlock, std::move(body));
}

std::vector<std::byte> EncodeInterfaceDescription() {
  std::vector<std::byte> body;
  AppendU16(body, kLinkTypeWiresharkUpperPdu);
  AppendU16(body, 0);
  AppendU32(body, kNoSnapLength);
  const std::byte microseconds{kMicrosecondsExponent};
  AppendOption(body, kIfTsresol, std::span(&microseconds, 1));
  return Block(kInterfaceDescriptionBlock, std::move(body));
}

std::vector<std::byte> EncodeEnhancedPacket(const networking::TappedMessage& message) {
  const bool outbound = message.direction == networking::TapDirection::kOutbound;
  const networking::TapEndpoint& source = outbound ? message.local : message.remote;
  const networking::TapEndpoint& destination = outbound ? message.remote : message.local;
  std::vector<std::byte> packet = ExportedPduTlvs(source, destination);
  packet.insert(packet.end(), message.payload.begin(), message.payload.end());

  const auto microseconds = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(message.time.time_since_epoch()).count());
  std::vector<std::byte> body;
  AppendU32(body, kInterfaceId);
  // The upper 32 bits, then the lower.
  AppendU32(body, static_cast<std::uint32_t>(microseconds >> kTimestampHalfBits));
  AppendU32(body, static_cast<std::uint32_t>(microseconds));
  AppendU32(body, static_cast<std::uint32_t>(packet.size()));
  AppendU32(body, static_cast<std::uint32_t>(packet.size()));
  body.insert(body.end(), packet.begin(), packet.end());
  PadToAlignment(body);
  std::vector<std::byte> flags;
  AppendU32(flags, outbound ? kEpbFlagsOutbound : kEpbFlagsInbound);
  AppendOption(body, kEpbFlags, flags);
  return Block(kEnhancedPacketBlock, std::move(body));
}

}  // namespace augusta::pcapng
