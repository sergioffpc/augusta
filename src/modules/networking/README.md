# networking

`augusta_networking`. Shared: linked by both the client and the server, through
the Link alone: no target but `augusta_link` (and the transport's own tests)
links it, which `checks/core_boundary.cmake` checks.

## Responsibility

Wraps GameNetworkingSockets for direct client-server connections over UDP: a
Client dials one server, a Server listens for a handful of peers. It moves raw
framed payloads, each send naming its Reliability, and every send and receive
says whether the transport accepted it, dropped it for its peer or failed
locally. Safe from any thread. In a Development build a Client or Server given a
Tap reports to it every send the transport accepted and every message received,
before it is decoded, with its endpoints and wall-clock time; the tap is called
on the sending or receiving thread and must not block.

## Not its responsibility

- What the bytes mean: protocol.
- Who may connect and what a peer may send: the server's admission and
  screening.
- Matchmaking, relays and peer-to-peer: out of scope.
- Retrying a send: the transport's reliable delivery.
- What a tap does with the messages, such as writing a pcapng file: the tap's.

## Decisions

[ADR-0003](../../../docs/adr/0003-networking-transport.md),
[ADR-0033](../../../docs/adr/0033-error-handling.md),
[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0074](../../../docs/adr/0074-debug-tools-compiled-out-of-shipping-builds.md).
