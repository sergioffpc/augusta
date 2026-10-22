# link

`augusta_link`, `augusta_link_messages`, `augusta_link_raw` and
`augusta_link_types`. Shared: linked by both the client and the server.

## Responsibility

The Link: the channel through which one Session exchanges messages with the
other side, owning the transport connection beneath, and the only place the
game's state is converted to and from the Networking Protocol's messages. The
client's end connects to the server; the server's listens and keeps one channel
per peer, naming each by a peer identity of its own and taking in each payload
as what a client says; a replay server opens one of its own. Each end sends and
receives the Link's own message vocabulary, in the engine's types, each message
with the reliability ADR-0038's table gives its type, a server's message encoded
once for as many peers as need it and a tick's Authoritative State readdressed
to each recipient without encoding its bodies again; it reads each connection's
round trip, loss and queue in its own types and keeps a Replay list within what
the protocol carries. Its conversions, which keep only the newest of a tick's
unacknowledged Commands within the protocol's bound and turn a Match capture's
records into their bytes and back, are stateless and have no transport beneath,
so the Host and capture convert without opening a connection. It owns the
transport's process-wide setup and, for tests only, its simulated network
conditions and raw ends that carry bytes as they are. Only it links networking
(`checks/core_boundary.cmake`), and privately, with the protocol, so whatever
speaks to it reaches neither. A Development build's messages, Debug subscribe
and Shot judgement, have their Link type, conversions and reliability only with
`AUGUSTA_DEBUG_TOOLS`.

## Not its responsibility

- The codec and the messages' bytes: protocol.
- Sockets and delivery: networking, beneath the Link alone.
- The Host's own types (its Match, replication) and mapping them into the
  Link's, and what a server does with a peer (admission, misbehaviour): host.
- A client's Session over the Link: harness.
- Reading and writing a capture's file, and the format's record order: capture.
- Whether a value is acceptable: whoever takes it in.

## Decisions

[ADR-0003](../../../docs/adr/0003-networking-transport.md),
[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0050](../../../docs/adr/0050-match-capture-and-reenactment.md),
[ADR-0051](../../../docs/adr/0051-replay.md),
[ADR-0074](../../../docs/adr/0074-debug-tools-compiled-out-of-shipping-builds.md).
