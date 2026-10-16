# capture

`augusta_capture`. Shared: linked by both the client and the server.

## Responsibility

Owns the Match capture file: reads one and holds it to the format's rules
(magic, format version, record order) and writes one, converting its records
between the Networking Protocol's encoding and the engine's types, so its
interface names no protocol type. Pure stream I/O.

## Not its responsibility

- Deciding what is captured, when, and how long a capture is kept: host
  (Capturer, capture retention, Replay).
- Playing a captured player again: harness (`augusta/reenactment.h`).

## Decisions

[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0050](../../../docs/adr/0050-match-capture-and-reenactment.md).
