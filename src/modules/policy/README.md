# policy

`augusta_policy` and `augusta_policy_loader`, both in `augusta::policy`.
Server-only: no client links them.

## Responsibility

A scenario's Game policy: its rules script's hooks (`on_tick`, `assign_spawns`),
the read-only views of the Match built for them, and the typed Policy actions
(`augusta/policy_actions.h`) their answers are read into, validated against what
each hook may decide. SimulationWorld and the loader call it; its interface
names no Lua type, so neither links scripting.

Loading the rules from the server pack (`augusta/policy_loader.h`), once at
startup or afresh for each Replay, is the loader's, a target of its own so
SimulationWorld never links the pack.

## Not its responsibility

- Embedding and sandboxing Lua: scripting.
- Verifying and reading the pack itself: assets.
- Acting on a Policy action: simulation, then the Host.

## Decisions

[ADR-0022](../../../docs/adr/0022-gameplay-scripting-language.md),
[ADR-0023](../../../docs/adr/0023-simulationworld-phase-pipeline.md).
