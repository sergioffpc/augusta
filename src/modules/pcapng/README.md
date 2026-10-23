# pcapng

`augusta_pcapng`. Shared: the client, the server and the replay server's
endpoint each write one. A debug tool: only a Development build has it
(ADR-0074).

## Responsibility

A Development build's pcapng of the messages it sends and receives, decrypted
(ADR-0076): the pure encoder of the file's blocks, a Section Header Block naming
the executable and the engine version, an Interface Description Block of link
type 252 (`LINKTYPE_WIRESHARK_UPPER_PDU`) and an Enhanced Packet Block per
message holding its `exported_pdu` TLVs, its payload and its direction; and the
file writer that implements the transport's tap over it, one file per run in the
directory it is given, written on a thread of its own behind a bounded queue,
which stops with one warning when the queue fills or the disk fails and never
stops the runtime, and is started for each executable's run, its file logged, by
one call when the run's config names a directory. It depends on networking's tap
types, the failure model's fault injection and logging alone.

## Not its responsibility

- Reporting each message sent and received, with its endpoints and time:
  networking's tap.
- Passing the tap to the transports: link, from each executable's runtime.
- Which directory the file goes in, and refusing one that can't be written: the
  `debug.network.pcapng` key of each executable's config.
- The engine version written in the file: version, through its caller.
- Reading the file: Wireshark, through `tools/wireshark`'s `augusta` dissector.
- Rotating, capping or deleting files: nobody; the directory is the developer's.

## Decisions

[ADR-0074](../../../docs/adr/0074-debug-tools-compiled-out-of-shipping-builds.md),
[ADR-0076](../../../docs/adr/0076-wireshark-reads-the-games-own-pcapng.md).
