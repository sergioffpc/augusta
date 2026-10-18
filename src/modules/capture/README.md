# capture

`augusta_capture`, `augusta_capture_records`. Shared: linked by both the client
and the server.

## Responsibility

Owns the Match capture file: what its records say in the engine's types, and
reading one, held to the format's rules (magic, format version, record order),
and writing one, each record framed by its length, its bytes the Link's to make
and read, so it names no protocol type. Pure stream I/O.

## Not its responsibility

- Deciding what is captured, when, and how long a capture is kept: host
  (Capturer, capture retention, Replay).
- Playing a captured player again: harness (`augusta/reenactment.h`).
- Each record's bytes, the magic and the header's format version: link
  (`augusta_link_messages`), the only target capture converts through.

## Decisions

[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0050](../../../docs/adr/0050-match-capture-and-reenactment.md).
