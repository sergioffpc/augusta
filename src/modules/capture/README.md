# capture

`augusta_capture`, `augusta_capture_records`. Shared: linked by both the client
and the server.

## Responsibility

Owns the Match capture file: what its records say in the engine's types, and
reading one, held to the format's rules (magic, format version, record order),
and writing one, in the Networking Protocol's encoding, so its interface names
no protocol type. Pure stream I/O.

## Not its responsibility

- Deciding what is captured, when, and how long a capture is kept: host
  (Capturer, capture retention, Replay).
- Playing a captured player again: harness (`augusta/reenactment.h`).
- Converting its records to and from the protocol's: link.

## Decisions

[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0050](../../../docs/adr/0050-match-capture-and-reenactment.md).
