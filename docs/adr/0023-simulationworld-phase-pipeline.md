# SimulationWorld Phase Pipeline

SimulationWorld runs nine ordered phases per tick: CommandIngestion, Movement, Dynamics, WeaponHandling, Ballistics, HitDetection, Damage, Scripts/Behaviours, Commit. Scripts/Behaviours runs last, after Damage resolves the tick's deaths, so it can evaluate win conditions and schedule Match end for the next tick. With no respawn in v1, Game policy assigns every player's Spawn point once, at Match start, through the same policy engine (the scenario's `assign_spawns` hook, ADR-0022). Match start is SimulationWorld's own entry point, outside the tick: the server decides when a Match starts (ADR-0043), and SimulationWorld runs `assign_spawns` and places every player's body at its Spawn point with fresh health and a full rifle, so nothing carries over from the previous Match. An answer that names no Spawn point the Map has, or is otherwise invalid, is refused and logged, and players take the Spawn points in order instead, starting over after the last. Dynamics runs the tick's one `simulate()` right after Movement (ADR-0045): it applies the impulses the previous tick's shots and explosions queued, and reports the Prop contacts that Damage resolves in the same tick.

## Consequences

Weapon/ammo damage values are data-driven configuration — not mechanism code or policy scripts — a third category alongside mechanism and policy.
