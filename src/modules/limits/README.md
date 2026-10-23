# limits

`augusta_limits`. Shared: linked by both the client and the server.

## Responsibility

The bounds the gameplay rules and the Networking Protocol both need - the
players a Lobby or a Match holds, the kicks a recoil pattern holds, the ticks a
Shot judgement tells - below both, so neither depends on the other for them.
Header-only; it depends on nothing but the standard library, and nothing defines
these again.

## Not its responsibility

- The Tick and the command Sequence: tick.
- How many Commands one message carries: protocol
  (`protocol::kMaxCommandsPerMessage`).

## Decisions

[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0043](../../../docs/adr/0043-lobby-and-match-lifecycle.md).
