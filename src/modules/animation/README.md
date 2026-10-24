# animation

`augusta_animation`. Shared: compiled into both the client and the server, below
presentation, depending on physics and gameplay at most.

## Responsibility

A Character's Skeleton posed: from a player's Hitbox pose, each of its Joints
relative to its parent and the world transform that composes, which places the
Hitboxes bound to them. Until a Character has Clips the pose is the Rest pose,
the stance and the facing carried by the Skeleton's root.

## Not its responsibility

- Loading a Skeleton and binding each Hitbox to its Joint: assets.
- Placing a Hitbox by its Joint's transform, and where a Hitbox pose puts a
  Character's root: gameplay (`augusta/hitbox.h`).
- Which players are posed and when, the Hitbox history and judging a round:
  simulation, which poses every player each tick.
- Loading a Character's Hitboxes for the Hitboxes Debug overlay: the client, in
  a Development build only (ADR-0074), its one other consumer. It places them in
  the Character's root space by the Rest pose once, when the character loads; no
  client World poses a player.
- Drawing a posed Character, and posing players on the client: presentation,
  once Clips and skinning arrive (ADR-0066).

## Decisions

[ADR-0023](../../../docs/adr/0023-simulationworld-phase-pipeline.md),
[ADR-0040](../../../docs/adr/0040-character-authoring-format.md),
[ADR-0061](../../../docs/adr/0061-skeletal-hitboxes.md).
