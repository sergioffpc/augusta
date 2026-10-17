# policy

`augusta_policy`. Server-only: no client links it.

## Responsibility

A scenario's Game policy: its rules script's hooks (`on_tick`, `assign_spawns`),
the read-only views of the Match built for them, and the typed Policy actions
(`augusta/policy_actions.h`) their answers are read into, validated against what
each hook may decide. SimulationWorld and the scenario loader call it; its
interface names no Lua type, so neither links scripting.

## Not its responsibility

- Embedding and sandboxing Lua: scripting.
- Loading the rules from the pack: scenario.
- Acting on a Policy action: simulation, then the Host.

## Decisions

[ADR-0022](../../../docs/adr/0022-gameplay-scripting-language.md),
[ADR-0023](../../../docs/adr/0023-simulationworld-phase-pipeline.md).
