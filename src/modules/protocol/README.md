# protocol

`augusta_protocol`. Shared: linked by both the client and the server.

## Responsibility

The Networking Protocol: the messages client and server exchange, and the
capture's records, with their custom binary encoding. A pure codec - bytes in, a
message or a typed error out - that treats its input as untrusted, and whose
messages hold only its own plain `*Wire` types.

## Not its responsibility

- Sockets and delivery: networking.
- Converting to the engine's types: wire (shared core), `augusta/harness_wire.h`
  (client), `src/server/wire.h` (server).
- Which peer may send what, and what a message means for the Match: the
  receiver.

## Decisions

[ADR-0007](../../../docs/adr/0007-serialization-format.md),
[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0050](../../../docs/adr/0050-match-capture-and-reenactment.md).
