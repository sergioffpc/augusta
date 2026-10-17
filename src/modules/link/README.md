# link

`augusta_link_messages`. Shared: linked by both the client and the server.

## Responsibility

The Link's conversions: every conversion between the game's state and the
Networking Protocol's messages, both ways, in the engine's types the messages
say (a join, an Authoritative State, a Shot, a tick's Commands, ...), with a
tick's Commands split into messages within the protocol's bound, and a Match
capture's records. Stateless functions with no transport beneath, so a module
converts without opening a connection or linking networking.

## Not its responsibility

- The codec and the messages' bytes: protocol.
- Sockets and delivery: networking.
- Each peer's own types (the Host's Match and replication, the harness's
  sessions) and building the Link's from them: host and harness.
- Reading and writing a capture's file, and the format's record order: capture.
- Whether a value is acceptable: whoever takes it in.

## Decisions

[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0050](../../../docs/adr/0050-match-capture-and-reenactment.md).
