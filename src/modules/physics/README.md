# physics

`augusta_physics`. Shared: linked by both the client and the server.

## Responsibility

Wraps PhysX for collision and movement: player bodies, stance, stamina, raycasts
against the Map, dynamic bodies, and restoring a body to a given state for
reconciliation. It builds a map's static collision from a verified pack
(`augusta/map.h`), keeping the opaque Surface id its caller adds each collision
mesh with and reporting it from the Map raycast, never interpreting it.
PredictionWorld and SimulationWorld use the same World.

In a Development build (ADR-0074) it also records the process's PhysX scenes to
one OmniPVD file from startup to exit (`StartRecording`), which
`debug.physics_recording` asks for on the server and the client. Only a Debug
build's PhysX records, through OmniPVD's runtime that the PhysX overlay port
(`ports/physx`) builds; elsewhere the recording is refused, saying why.

## Not its responsibility

- Bullet trajectories: gameplay (`augusta/ballistics.h`), not PhysX.
- Bit-exact agreement between client and server: not guaranteed; reconciliation
  corrects the client.
- Particles: renderer.

## Decisions

[ADR-0002](../../../docs/adr/0002-physics-and-ballistics.md),
[ADR-0018](../../../docs/adr/0018-runtime-asset-format.md),
[ADR-0045](../../../docs/adr/0045-dynamic-bodies.md).
