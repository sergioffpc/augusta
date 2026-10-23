#ifndef AUGUSTA_PCAPNG_H_
#define AUGUSTA_PCAPNG_H_

#include <cstddef>
#include <string_view>
#include <vector>

#include "augusta/networking.h"

/// \file
/// The pcapng blocks of a Development build's file of the messages it sends and
/// receives (ADR-0076), as bytes: a pure encoder, free of files and threads, so
/// its bytes are tested apart from them. The file writer (pcapng_file_writer.h)
/// writes them; Wireshark reads them through tools/wireshark's `augusta`
/// dissector. Every block is little-endian, the section's byte order, and padded
/// to 4 bytes. A debug tool: only a Development build has it (ADR-0074).
namespace augusta::pcapng {

/// The file's Section Header Block, whose `shb_userappl` is
/// `<executable> <version>` (e.g. `augustac 1.2.3+dev`): which build's `.proto`
/// to read the file with. Its section length is left unspecified.
std::vector<std::byte> EncodeSectionHeader(std::string_view executable, std::string_view version);

/// The file's one Interface Description Block, interface 0 of every Enhanced
/// Packet Block: link type 252 (`LINKTYPE_WIRESHARK_UPPER_PDU`), whose packets
/// begin with `exported_pdu` TLVs, timestamps in microseconds, no snap length.
std::vector<std::byte> EncodeInterfaceDescription();

/// One message's Enhanced Packet Block on interface 0: its `exported_pdu` TLVs,
/// big-endian as Wireshark reads them, naming the `augusta` dissector and the
/// source and destination address and port, then its payload, with its
/// direction in `epb_flags`. Source and destination follow the direction: an
/// outbound message goes local to remote, an inbound one remote to local. The
/// timestamp is message's time in microseconds since the epoch.
std::vector<std::byte> EncodeEnhancedPacket(const networking::TappedMessage& message);

}  // namespace augusta::pcapng

#endif  // AUGUSTA_PCAPNG_H_
