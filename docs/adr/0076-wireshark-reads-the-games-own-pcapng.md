# Wireshark Reads the Game's Own Pcapng of Decrypted Messages

GameNetworkingSockets encrypts every connection (ADR-0003), so a capture of the
wire shows ciphertext, and even a plaintext wire carries GNS's SNP framing, not
Augusta's messages. A developer chasing a networking bug needs to see which
message went where, when, with which fields. A Development build therefore
writes its own pcapng of the messages it sends and receives, decrypted, and
Wireshark reads it through a fixed Lua shim over its built-in `protobuf`
dissector. Like every debug tool, it exists only in a Development build
(ADR-0074).

**The game writes the file.** Each executable, the replay server's endpoint
included, writes one pcapng per run into the directory its config's
`debug.network.pcapng` names; without the key it writes nothing. One packet is
one message (ADR-0038): every message the transport accepted to send, and every
payload it received, before anything decodes it, so a payload that fails to
decode is recorded too. GNS's encryption stays as it is: debugging never changes
the connection it debugs.

**`exported_pdu`, link type 252.** Each packet is a
`LINKTYPE_WIRESHARK_UPPER_PDU` record whose TLVs name the `augusta` dissector
and carry both endpoints' IPv4 or IPv6 addresses and ports, so Wireshark sets
addresses and conversations from them and calls the dissector by name. The
direction goes in `epb_flags`, which Wireshark shows as `pinfo.p2p_dir`. The
timestamp is the wall clock when the transport took or handed over the message,
so the file lines up with logs and other captures.

**The transport only taps.** `networking::Client` and `networking::Server` take
an optional tap at construction, only with `AUGUSTA_DEBUG_TOOLS`, called once
per message with its direction, endpoints, payload and time. Networking stays a
transport and knows nothing of pcapng. The pcapng blocks are encoded by a pure
module, `augusta_pcapng`, linked only in a Development build; its file writer
implements the tap and hands each packet to a thread of its own through a
bounded queue, so no tick and no Network I/O thread waits for the disk. A full
queue or a failed write stops the file there with one warning, keeping every
packet before it; it never stops or degrades the runtime.

**A fixed Lua shim over `protobuf`.** The `augusta` dissector,
`tools/wireshark/augusta.lua`, registers `Proto("augusta")` and hands each whole
payload to Wireshark's `protobuf` dissector as `augusta.Message`, read from the
protocol's one `.proto` (ADR-0007). It holds no field names and parses nothing
itself, so it never changes when the protocol does: the envelope's `oneof` names
every message, Development messages included. It is Lua, so it needs no build
and runs on any Wireshark 4.4 or later. `tools/wireshark/install.sh` puts it in
the personal Lua plugins folder and adds the protocol's `proto/` directory to
the profile's Protobuf search paths, idempotently.

**A structural `tshark` test.** CI runs `tshark` with the shim over a pcapng the
game's own writer made of the protocol's fuzz seeds, one packet per message
kind, committed beside them and checked for drift by a C++ test. Every packet
must decode as the message its seed names, with no malformed and no undecoded
bytes. Field values need no oracle, since the writer, the codec and the
dissector share one schema. The test skips without `tshark` locally and fails
without it in CI.

**The version is shown, not enforced.** The section header's `shb_userappl`
names the executable and the engine version, `+dev` included, so a developer
knows which build's `.proto` to read the file with. Nothing refuses a mismatch:
an older capture still opens, at the developer's own risk.

## Considered Options

- **GNS's encryption off** (`k_ESteamNetworkingConfig_Unencrypted`): a capture
  of the real wire, but both peers must set it, the connection debugged is no
  longer the one played, and the dissector must parse all of GNS's protocol
  (handshakes, SNP frames, acks, lanes) and reassemble fragments and a
  retransmitted reliable stream before it reaches one message.
- **Exported session keys**: the real, encrypted wire, but GNS has no API to
  export its keys, so it means patching GNS, and pcapng's Decryption Secrets
  Block knows no GNS, so the dissector would do everything the option above
  needs plus AES-GCM.
- **A C plugin dissector**: faster and with Wireshark's reassembly, which this
  file does not need, but rebuilt for every Wireshark minor release and every
  platform, and compiled in CI against one exact version.
- **A user DLT** (147-162): no `exported_pdu` TLVs, but each developer must map
  it to the dissector by hand, and the packets carry no addresses or ports.
- **Converting Match captures** (ADR-0050): no new writer, but a capture holds
  client actions only, not messages, and no server-to-client traffic at all.

## Consequences

- A Shipping build has neither the tap, the writer nor the key; its config
  refuses `debug.network.pcapng` as it refuses the whole `debug` section.
- The dissector follows the protocol for free, but only while the top level
  stays one `augusta.Message` envelope (ADR-0007); a second top-level type would
  change the shim.
- The client's local endpoint is unspecified in its file, since GNS does not
  give a connection's local address.
- The fuzz seeds' pcapng is regenerated whenever the seeds are, so the CI test
  always covers every message kind.
- Nothing rotates, caps or deletes the files: the directory is the developer's.
