# math

`augusta_math`. Shared: linked by both the client and the server.

## Responsibility

The engine's vector math: a thin facade over GLM with the types and operations
module interfaces need, under the project's own names, so no module depends on
GLM directly, and the grids those numbers are counted on (`augusta/grid.h`), so
physics and the protocol never disagree on one. Header-only.

## Not its responsibility

- The arithmetic itself: GLM.
- Movement, collision and trajectories: physics and gameplay.

## Decisions

[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md).
