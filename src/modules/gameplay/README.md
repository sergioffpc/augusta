# gameplay

`augusta_gameplay`. Shared: linked by both the client and the server.

## Responsibility

The gameplay rules both Worlds apply identically, a header and namespace per
concept: the Command (one tick's player intent), the Parameters (only the
aggregate of its sections and its validation: each section is typed by the
concept it configures), the rifle and its ammunition, the Body part and bullet
trajectories. Pure, deterministic mechanism with no world or state of its own.

## Not its responsibility

- Running the rules over a tick's entities: simulation (server) and prediction
  (client).
- Sampling a device into a Command: input.
- Deciding which player a bullet hits and for what damage: simulation.
- Where the Parameters' values come from: the server.
- The Tick, the command Sequence and the fixed tick schedule: tick.
- The player and recoil-kick bounds: limits.
- Encoding any of it: protocol and wire.

## Decisions

[ADR-0002](../../../docs/adr/0002-physics-and-ballistics.md),
[ADR-0005](../../../docs/adr/0005-threading-model.md),
[ADR-0023](../../../docs/adr/0023-simulationworld-phase-pipeline.md),
[ADR-0024](../../../docs/adr/0024-client-world-phase-pipelines.md),
[ADR-0039](../../../docs/adr/0039-lua-data-driven-configuration.md),
[ADR-0044](../../../docs/adr/0044-shot-lag-compensation-and-replication.md).
