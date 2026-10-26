# simulation

`augusta_simulation`. Server-only: no client links it.

## Responsibility

Orchestrates SimulationWorld: the single authoritative ECS pipeline, run once
per tick on the Simulation thread, with players and bullets as entities, hit
detection against hitboxes as they were the Shooter's delay ago, where each
player's rifle is held after its Posing phase, and the Scripts/Behaviours phase,
which calls Game policy and hands its typed actions (`augusta/policy_actions.h`)
on in the tick's result. It decides what each client is sent
(`augusta/replication.h`), and, in a Development build, what each round was
judged against (its Shot judgement).

## Not its responsibility

- Screening commands, encoding and sending state: the server (`host::Host`).
- Whether Shot judgements are built, and which peer is sent each: the server
  (`host::Host`), from `debug.shot_judgements` and the peer's Debug subscribe.
- The movement, weapon and ballistics rules, placing a player's Hitboxes by
  their Joints, and where a hand puts a rifle's Muzzle: physics and gameplay.
- Posing a Character's Skeleton, which its Posing phase calls: animation.
- Game policy's hooks, their views and reading their answers: policy.

## Decisions

[ADR-0001](../../../docs/adr/0001-ecs-library.md),
[ADR-0022](../../../docs/adr/0022-gameplay-scripting-language.md),
[ADR-0023](../../../docs/adr/0023-simulationworld-phase-pipeline.md),
[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0044](../../../docs/adr/0044-shot-lag-compensation-and-replication.md),
[ADR-0045](../../../docs/adr/0045-dynamic-bodies.md),
[ADR-0061](../../../docs/adr/0061-skeletal-hitboxes.md),
[ADR-0064](../../../docs/adr/0064-muzzle.md),
[ADR-0074](../../../docs/adr/0074-debug-tools-compiled-out-of-shipping-builds.md).
