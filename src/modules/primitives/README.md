# primitives

`augusta_primitives`. Shared: linked by both the client and the server.

## Responsibility

The counters and bounds the engine's modules and the Networking Protocol both
need - the Tick and Sequence widths, the player, command and recoil bounds -
below both, so neither depends on the other for them. Header-only; it depends on
nothing but the standard library, and nothing defines these again.

## Not its responsibility

- The grids numbers live on: math (`augusta/grid.h`).
- Messages: protocol.

## Decisions

[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md).
