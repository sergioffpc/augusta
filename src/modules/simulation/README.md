# simulation

`augusta_simulation`. Server-only: no client links it.

## Responsibility

Orchestrates SimulationWorld: the single authoritative ECS pipeline, run once
per tick on the Simulation thread, with players and bullets as entities, hit
detection against hitboxes as they were the Shooter's delay ago, and the
Scripts/Behaviours phase, which calls Game policy and hands its typed actions
(`augusta/policy_actions.h`) on in the tick's result. It decides what each
client is sent (`augusta/replication.h`).

## Not its responsibility

- Screening commands, encoding and sending state: the server (`server::Host`).
- The movement, weapon and ballistics rules: physics and gameplay.
- Game policy's hooks, their views and reading their answers: policy.

## Decisions

[ADR-0001](../../../docs/adr/0001-ecs-library.md),
[ADR-0022](../../../docs/adr/0022-gameplay-scripting-language.md),
[ADR-0023](../../../docs/adr/0023-simulationworld-phase-pipeline.md),
[ADR-0044](../../../docs/adr/0044-shot-lag-compensation-and-replication.md),
[ADR-0045](../../../docs/adr/0045-dynamic-bodies.md).
