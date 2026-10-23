# prediction

`augusta_prediction`. Client-only: the headless server never links it.

## Responsibility

Orchestrates PredictionWorld: the client's ECS pipeline, run once per fixed tick
on the Prediction thread, predicting the local player only. It reconciles
against the server's state by replaying the unacknowledged commands
(`augusta/reconciliation.h`) and emits an immutable Prediction State each tick.
It also holds the one definition of a correction: it adds up, from the
Prediction States' running totals, how often and how far Reconciliation moved
the body and how often it corrected the rifle, for the harness's netcode
statistics and the client's Network Debug panel alike.

## Not its responsibility

- Remote players and smoothing for display: presentation.
- Bullet trajectories: gameplay. Hits and damage: simulation.
- Talking to the server: harness.
- The movement and weapon rules: physics and gameplay.

## Decisions

[ADR-0001](../../../docs/adr/0001-ecs-library.md),
[ADR-0004](../../../docs/adr/0004-reconciliation-model.md),
[ADR-0005](../../../docs/adr/0005-threading-model.md),
[ADR-0024](../../../docs/adr/0024-client-world-phase-pipelines.md).
