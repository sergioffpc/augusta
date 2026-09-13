# SimulationWorld Phase Pipeline

SimulationWorld runs eight ordered phases per tick: CommandIngestion, Movement, WeaponHandling, Ballistics, HitDetection, Damage, Scripts/Behaviours, Commit. Scripts/Behaviours runs last, after Damage resolves the tick's deaths, so it can evaluate win conditions and schedule round transitions/spawns for the next tick.

## Consequences

Weapon/ammo damage values are data-driven configuration — not mechanism code or policy scripts — a third category alongside mechanism and policy.
