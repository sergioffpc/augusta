# link

`augusta_link`, `augusta_link_messages` and `augusta_link_types`. Shared: linked
by both the client and the server.

## Responsibility

The Link: the channel through which one Session exchanges messages with the
other side, owning the transport connection beneath.

- `augusta_link` (`augusta/link.h`): the client's end, which connects to the
  server, sends and receives the engine's types, each message with the
  reliability ADR-0038's table gives its type, and reads the connection's round
  trip, loss and queue in its own types. The conversions, the protocol and
  networking are linked privately, so whatever speaks to it reaches none of
  them.
- `augusta_link_messages` (`augusta/link_messages.h`): the Link's conversions,
  every one between the game's state and the Networking Protocol's messages,
  both ways, keeping only the newest of a tick's unacknowledged Commands within
  the protocol's bound in its one message, and a Match capture's records.
  Stateless functions with no transport beneath, so a module converts without
  opening a connection or linking networking.
- `augusta_link_types` (`augusta/link_types.h`): what each message says, in the
  engine's types (a join, an Authoritative State, a Shot, a tick's Commands,
  ...).

## Not its responsibility

- The codec and the messages' bytes: protocol.
- Sockets and delivery: networking, beneath `augusta_link` alone.
- The server's end, its listening socket and its channel per peer: the Host
  still holds them over networking.
- Each peer's own types (the Host's Match and replication, the harness's
  sessions) and building the Link's from them: host and harness.
- Reading and writing a capture's file, and the format's record order: capture.
- Whether a value is acceptable: whoever takes it in.

## Decisions

[ADR-0003](../../../docs/adr/0003-networking-transport.md),
[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0050](../../../docs/adr/0050-match-capture-and-reenactment.md).
