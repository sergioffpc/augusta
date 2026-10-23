#include "augusta/pcapng.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/networking.h"

// The pcapng blocks augusta_pcapng encodes (ADR-0076, Development build only),
// against bytes worked out by hand from the pcapng specification
// (draft-ietf-opsawg-pcapng) and Wireshark's exported_pdu TLVs
// (wsutil/exported_pdu_tlvs.h): every block little-endian, every exported_pdu
// TLV big-endian.
namespace augusta::pcapng {
namespace {

std::vector<std::byte> BytesOf(std::initializer_list<int> values) {
  std::vector<std::byte> bytes;
  bytes.reserve(values.size());
  for (const int value : values) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return bytes;
}

TEST(PcapngSectionHeader, NamesTheExecutableAndItsVersionInShbUserappl) {
  const std::vector<std::byte> expected = BytesOf({
      0x0A, 0x0D, 0x0D, 0x0A,                          // Block Type: Section Header Block.
      0x38, 0x00, 0x00, 0x00,                          // Block Total Length: 56.
      0x4D, 0x3C, 0x2B, 0x1A,                          // Byte-Order Magic, little-endian.
      0x01, 0x00, 0x00, 0x00,                          // Version 1.0.
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Section Length: unspecified.
      0x04, 0x00, 0x12, 0x00,                          // shb_userappl, 18 bytes,
      'a',  'u',  'g',  'u',  's',  't',  'a',  'c',  ' ',  '1',
      '.',  '2',  '.',  '3',  '+',  'd',  'e',  'v',  0x00, 0x00,  // padded to 20.
      0x00, 0x00, 0x00, 0x00,                                      // opt_endofopt.
      0x38, 0x00, 0x00, 0x00,                                      // Block Total Length: 56.
  });

  EXPECT_EQ(EncodeSectionHeader("augustac", "1.2.3+dev"), expected);
}

TEST(PcapngInterfaceDescription, IsAnUpperPduInterfaceInMicroseconds) {
  const std::vector<std::byte> expected = BytesOf({
      0x01, 0x00, 0x00, 0x00,  // Block Type: Interface Description Block.
      0x20, 0x00, 0x00, 0x00,  // Block Total Length: 32.
      0xFC, 0x00,              // LinkType: 252, LINKTYPE_WIRESHARK_UPPER_PDU.
      0x00, 0x00,              // Reserved.
      0x00, 0x00, 0x00, 0x00,  // SnapLen: no limit.
      0x09, 0x00, 0x01, 0x00,  // if_tsresol, 1 byte:
      0x06, 0x00, 0x00, 0x00,  // 10^-6 seconds, padded to 4.
      0x00, 0x00, 0x00, 0x00,  // opt_endofopt.
      0x20, 0x00, 0x00, 0x00,  // Block Total Length: 32.
  });

  EXPECT_EQ(EncodeInterfaceDescription(), expected);
}

constexpr networking::Ipv4Address kLocalIpv4{10, 0, 0, 1};
constexpr networking::Ipv4Address kRemoteIpv4{192, 168, 1, 10};
constexpr std::uint16_t kLocalPort = 7777;    // 0x1E61.
constexpr std::uint16_t kRemotePort = 27015;  // 0x6987.
// 1700000000.123456 s since the epoch: 0x00060A24'18202240 microseconds.
constexpr std::chrono::system_clock::time_point kTime{std::chrono::microseconds(1'700'000'000'123'456)};

TEST(PcapngEnhancedPacket, AnOutboundIpv4MessageGoesFromLocalToRemote) {
  const std::vector<std::byte> payload = BytesOf({'a', 'b'});
  const networking::TappedMessage message{
      .direction = networking::TapDirection::kOutbound,
      .local = {.address = kLocalIpv4, .port = kLocalPort},
      .remote = {.address = kRemoteIpv4, .port = kRemotePort},
      .payload = payload,
      .time = kTime,
  };
  const std::vector<std::byte> expected = BytesOf({
      0x06,
      0x00,
      0x00,
      0x00,  // Block Type: Enhanced Packet Block.
      0x60,
      0x00,
      0x00,
      0x00,  // Block Total Length: 96.
      0x00,
      0x00,
      0x00,
      0x00,  // Interface ID: 0.
      0x24,
      0x0A,
      0x06,
      0x00,  // Timestamp (Upper).
      0x40,
      0x22,
      0x20,
      0x18,  // Timestamp (Lower).
      0x32,
      0x00,
      0x00,
      0x00,  // Captured Packet Length: 50.
      0x32,
      0x00,
      0x00,
      0x00,  // Original Packet Length: 50.
      // exported_pdu TLVs, big-endian:
      0x00,
      0x0C,
      0x00,
      0x08,  // DISSECTOR_NAME, 8 bytes:
      'a',
      'u',
      'g',
      'u',
      's',
      't',
      'a',
      0x00,  // "augusta", padded to 8.
      0x00,
      0x14,
      0x00,
      0x04,  // IPV4_SRC:
      10,
      0,
      0,
      1,  // local.
      0x00,
      0x15,
      0x00,
      0x04,  // IPV4_DST:
      192,
      168,
      1,
      10,  // remote.
      0x00,
      0x19,
      0x00,
      0x04,  // SRC_PORT:
      0x00,
      0x00,
      0x1E,
      0x61,  // 7777.
      0x00,
      0x1A,
      0x00,
      0x04,  // DST_PORT:
      0x00,
      0x00,
      0x69,
      0x87,  // 27015.
      0x00,
      0x00,
      0x00,
      0x00,  // END_OF_OPT.
      'a',
      'b',
      0x00,
      0x00,  // The payload, padded to 4.
      0x02,
      0x00,
      0x04,
      0x00,  // epb_flags, 4 bytes:
      0x02,
      0x00,
      0x00,
      0x00,  // outbound.
      0x00,
      0x00,
      0x00,
      0x00,  // opt_endofopt.
      0x60,
      0x00,
      0x00,
      0x00,  // Block Total Length: 96.
  });

  EXPECT_EQ(EncodeEnhancedPacket(message), expected);
}

TEST(PcapngEnhancedPacket, AnInboundIpv6MessageGoesFromRemoteToLocal) {
  // ::1 and 2001:db8::7.
  constexpr networking::Ipv6Address kLocalIpv6{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  constexpr networking::Ipv6Address kRemoteIpv6{0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7};
  const networking::TappedMessage message{
      .direction = networking::TapDirection::kInbound,
      .local = {.address = kLocalIpv6, .port = kLocalPort},
      .remote = {.address = kRemoteIpv6, .port = kRemotePort},
      .payload = {},
      .time = kTime,
  };
  const std::vector<std::byte> expected = BytesOf({
      0x06, 0x00, 0x00, 0x00,                       // Block Type: Enhanced Packet Block.
      0x74, 0x00, 0x00, 0x00,                       // Block Total Length: 116.
      0x00, 0x00, 0x00, 0x00,                       // Interface ID: 0.
      0x24, 0x0A, 0x06, 0x00,                       // Timestamp (Upper).
      0x40, 0x22, 0x20, 0x18,                       // Timestamp (Lower).
      0x48, 0x00, 0x00, 0x00,                       // Captured Packet Length: 72.
      0x48, 0x00, 0x00, 0x00,                       // Original Packet Length: 72.
      0x00, 0x0C, 0x00, 0x08,                       // DISSECTOR_NAME, 8 bytes:
      'a',  'u',  'g',  'u',  's', 't', 'a', 0x00,  // "augusta", padded to 8.
      0x00, 0x16, 0x00, 0x10,                       // IPV6_SRC: remote.
      0x20, 0x01, 0x0D, 0xB8, 0,   0,   0,   0,    0, 0, 0, 0, 0, 0, 0, 7, 0x00, 0x17, 0x00, 0x10,  // IPV6_DST: local.
      0,    0,    0,    0,    0,   0,   0,   0,    0, 0, 0, 0, 0, 0, 0, 1, 0x00, 0x19, 0x00, 0x04,  // SRC_PORT:
      0x00, 0x00, 0x69, 0x87,                                                                       // 27015.
      0x00, 0x1A, 0x00, 0x04,                                                                       // DST_PORT:
      0x00, 0x00, 0x1E, 0x61,                                                                       // 7777.
      0x00, 0x00, 0x00, 0x00,  // END_OF_OPT, then no payload.
      0x02, 0x00, 0x04, 0x00,  // epb_flags, 4 bytes:
      0x01, 0x00, 0x00, 0x00,  // inbound.
      0x00, 0x00, 0x00, 0x00,  // opt_endofopt.
      0x74, 0x00, 0x00, 0x00,  // Block Total Length: 116.
  });

  EXPECT_EQ(EncodeEnhancedPacket(message), expected);
}

// The offset of an IPv4 packet's payload in its Enhanced Packet Block: the
// block's 28 bytes of header, then 48 of exported_pdu TLVs.
constexpr std::size_t kIpv4PayloadOffset = 28 + 48;

networking::TappedMessage Ipv4Message(networking::TapDirection direction, std::span<const std::byte> payload) {
  return {
      .direction = direction,
      .local = {.address = kLocalIpv4, .port = kLocalPort},
      .remote = {.address = kRemoteIpv4, .port = kRemotePort},
      .payload = payload,
      .time = kTime,
  };
}

TEST(PcapngEnhancedPacket, AnInboundIpv4MessageSwapsSourceAndDestination) {
  const std::vector<std::byte> block = EncodeEnhancedPacket(Ipv4Message(networking::TapDirection::kInbound, {}));

  // IPV4_SRC, IPV4_DST, SRC_PORT and DST_PORT, after the dissector's name.
  const std::vector<std::byte> expected = BytesOf({
      0x00, 0x14, 0x00, 0x04, 192,  168,  1,    10,    // remote,
      0x00, 0x15, 0x00, 0x04, 10,   0,    0,    1,     // to local,
      0x00, 0x19, 0x00, 0x04, 0x00, 0x00, 0x69, 0x87,  // from 27015
      0x00, 0x1A, 0x00, 0x04, 0x00, 0x00, 0x1E, 0x61,  // to 7777.
  });
  constexpr std::size_t kAddressesOffset = 28 + 12;
  ASSERT_GE(block.size(), kAddressesOffset + expected.size());
  EXPECT_EQ(std::vector(block.begin() + kAddressesOffset, block.begin() + kAddressesOffset + expected.size()),
            expected);
}

TEST(PcapngEnhancedPacket, PadsEveryPayloadLengthToFourBytes) {
  // Payload length, then the block's total length: 28 of header, 48 of TLVs,
  // the payload padded to 4, 8 of epb_flags, 4 of opt_endofopt, 4 of length.
  const std::vector<std::pair<std::size_t, std::size_t>> cases = {
      {0, 92}, {1, 96}, {2, 96}, {3, 96}, {4, 96},
  };
  for (const auto& [length, total] : cases) {
    const std::vector<std::byte> payload(length, std::byte{0xAB});

    const std::vector<std::byte> block =
        EncodeEnhancedPacket(Ipv4Message(networking::TapDirection::kOutbound, payload));

    ASSERT_EQ(block.size(), total) << "payload length " << length;
    // Captured and original length are the payload's own, after the TLVs.
    EXPECT_EQ(block[20], static_cast<std::byte>(48 + length)) << "payload length " << length;
    EXPECT_EQ(block[24], static_cast<std::byte>(48 + length)) << "payload length " << length;
    for (std::size_t i = kIpv4PayloadOffset; i < kIpv4PayloadOffset + length; ++i) {
      EXPECT_EQ(block[i], std::byte{0xAB}) << "payload length " << length << " byte " << i;
    }
    // Zeroes up to epb_flags' code, 2.
    for (std::size_t i = kIpv4PayloadOffset + length; i < total - 16; ++i) {
      EXPECT_EQ(block[i], std::byte{0}) << "payload length " << length << " byte " << i;
    }
    EXPECT_EQ(block[total - 16], std::byte{0x02}) << "payload length " << length;
  }
}

TEST(PcapngEnhancedPacket, SplitsTheTimestampIntoItsUpperAndLowerHalves) {
  // 2^32 + 1 microseconds, then 2^32 - 1.
  const std::vector<std::pair<std::chrono::microseconds, std::vector<std::byte>>> cases = {
      {std::chrono::microseconds(0x1'00000001), BytesOf({0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00})},
      {std::chrono::microseconds(0x0'FFFFFFFF), BytesOf({0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF})},
  };
  for (const auto& [since_epoch, expected] : cases) {
    networking::TappedMessage message = Ipv4Message(networking::TapDirection::kOutbound, {});
    message.time = std::chrono::system_clock::time_point(since_epoch);

    const std::vector<std::byte> block = EncodeEnhancedPacket(message);

    EXPECT_EQ(std::vector(block.begin() + 12, block.begin() + 20), expected) << since_epoch;
  }
}

}  // namespace
}  // namespace augusta::pcapng
