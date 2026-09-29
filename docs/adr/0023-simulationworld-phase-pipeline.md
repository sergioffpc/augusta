# SimulationWorld Phase Pipeline

SimulationWorld runs nine ordered phases per tick: CommandIngestion, Movement, Dynamics, WeaponHandling, Ballistics, HitDetection, Damage, Scripts/Behaviours, Commit. Scripts/Behaviours runs last, after Damage resolves the tick's deaths, so it can evaluate win conditions and schedule Match end and spawns for the next tick. Dynamics runs the tick's one `simulate()` right after Movement (ADR-0045): it applies the impulses the previous tick's shots and explosions queued, and reports the Prop contacts that Damage resolves in the same tick.

## Consequences

Weapon/ammo damage values are data-driven configuration — not mechanism code or policy scripts — a third category alongside mechanism and policy.
