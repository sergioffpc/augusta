# Serialization Format: Protocol Buffers

The Networking Protocol's messages and a Match capture's records (ADR-0038,
ADR-0050) are Protocol Buffers: a robust, widely tested format, less codec to
write and keep by hand, and a schema the debug tooling's Wireshark dissector
reads as it is (#913). What travels, and why, stays the protocol's own design:
the catalogue, the grids its numbers travel on, the reliability split and the
Link (ADR-0038). Only the bytes are protobuf's. A pack's contents are not
messages and keep a format of their own (ADR-0031).

**One schema, generated in the build.** One `.proto` in `src/modules/protocol/`,
`syntax = "proto3"` (Wireshark's `protobuf` dissector reads editions only from
4.6) and `package augusta`, describes the whole catalogue, a Development build's
messages included (ADR-0074), since a `oneof` cannot span files.
`protobuf_generate` turns it into C++ in the build; the generated code is never
checked in, and the `.proto` is its one source, the one Wireshark reads too. The
protobuf version is vcpkg's (protobuf 6.33.4 at the pinned submodule), and its
`protoc` builds natively under every preset.

**One envelope each.** A message is one `augusta.Message { oneof body { ... } }`
and nothing else, so the Wireshark shim never changes when the protocol does. A
capture's record is one `augusta.CaptureRecord { oneof record { ... } }`, never
an `augusta.Message`: a record is not a message, and neither decoder yields the
other's (ADR-0050).

**The protocol's types stay hand-written; the generated code is private.**
`augusta/protocol.h` keeps the `*Wire` types and the codec's API (`Encode`,
`Decode`, `Readdress`, `EncodeCaptureRecord`, `DecodeCaptureRecord`).
`protocol.cpp` maps each `*Wire` to its generated message and back, and checks
the grids' ranges and the protocol's limits in that mapping. `augusta_protocol`
links the full protobuf runtime `PRIVATE`, the one GameNetworkingSockets already
links into every networking binary (ADR-0003), never lite as well, which would
put two copies in one process. No header outside `src/modules/protocol` includes
a protobuf header or the generated one, which `checks/protocol_boundary.cmake`
enforces (ADR-0038). Each `Encode`, `Decode` and `Readdress` builds its message
on an arena local to the call, which never appears in the API: one allocation
per encode (the payload) and none per parse.

**The flat layout.** The hot-path messages are laid out to cost what the custom
format did:

- a grid's axes are inline fields, `sint32` for a signed count and `uint32` for
  an unsigned one, never a nested `Vec3`, which costs a tag, a length and, off
  an arena, an allocation per use;
- flags and a small enumeration share one `uint32` bitfield, as one byte held
  them before, and the bits no field uses are 0;
- the numbers that must arrive exactly (the Parameters, a rifle's two times, a
  player's health) are `float`;
- a Tick and a Sequence are `uint64`;
- a Session ID is `bytes` holding its UUIDv7's 16 bytes (ADR-0038);
- text is `bytes` holding UTF-8, which the codec checks itself, not `string`,
  whose check in protobuf's parse logs every failure (ADR-0038);
- every proto enum's 0 is `*_UNSPECIFIED`, which no sender writes;
- the two envelopes' `oneof` members number from disjoint ranges,
  `augusta.Message`'s from 1, each numbered as its message type, and
  `augusta.CaptureRecord`'s from 1000, each 1000 plus its record type: an
  unknown field is ignored, so a payload of one envelope sets no member of the
  other, and each decoder refuses it as a body it does not know.

An Authoritative State is 229 bytes typical against the custom format's 239, and
up to 415 with every number at its grid's bound, about 25 KB/s per client at 60
Hz in that extreme (#936).

**One message, one meaning, not one encoding.** Protobuf has no canonical bytes:
deterministic serialization holds only within one binary, and a parse accepts
fields in any order, repeats and overlong varints. So a message may arrive in
any valid encoding, and `Decode` takes any that maps to a valid message
(ADR-0038 says what it refuses). Fields the build does not know are ignored, at
every level, and an absent field or sub-message reads as its default. The values
keep their exactness: a count on a grid is the same value however its varint was
written.

**The schema is not a compatibility mechanism.** Builds never mix: a peer is
admitted only on an exact match of the engine version (ADR-0038), and a capture
is read only in its format's version (ADR-0050). That unknown fields decode is
what lets a later build's Join request reach its version check, not a promise
that two builds interoperate. A field removed from the schema is `reserved`, its
number never given to another, so a packet captured from an older build is never
read in Wireshark with another field's meaning.

## Considered Options

- **The custom binary format kept** (a type byte and fixed-width little-endian
  fields): rejected - about 1100 lines of codec written and maintained by hand,
  and no schema for the Wireshark dissector, which would have to follow every
  change by hand.
- **FlatBuffers**: rejected - its zero-copy reads matter little for messages of
  a few hundred bytes decoded once, its fixed-width tables make every message
  larger than varints do, and Wireshark has no built-in dissector for it.
- **The lite runtime**: rejected - the full runtime is already in every
  networking binary through GameNetworkingSockets, so lite saves nothing per
  process, and linking both puts two copies in one.
- **The generated classes as the protocol's types**: rejected - protobuf headers
  would reach `augusta/link_messages.h` and an arena's lifetime the Link, to
  guard a boundary the Link already holds; the mapping costs about 70 to 100 ns
  per decode (#939).
- **A natural layout** (a nested `Vec3`, a `bool` per flag, an enum stance):
  rejected - 279 bytes for a typical Authoritative State against 229 flat, and
  without an arena 38 allocations each way against 13 (#936).
- **A strict decode** (refusing unknown fields, re-serializing and comparing the
  bytes): rejected - one serialization more per decode, roughly doubling the
  server's decode per client, for bytes canonical only for one protobuf version,
  and no invariant of the game needs them (#940).
- **Structure-aware fuzzing (libprotobuf-mutator)**: rejected - a new
  dependency, and it stops feeding the parser broken bytes, the surface NFR-05
  is about; fuzzing stays byte-level (ADR-0013).
