# animation

`augusta_animation`. Shared: compiled into both the client and the server, below
presentation, depending on physics and gameplay at most, and on ozz-animation's
runtime (ADR-0066).

## Responsibility

A Character's Skeleton posed: from a player's Hitbox pose, each of its Joints
relative to its parent and the world transform that composes, which places the
Hitboxes bound to them. The pose is evaluated from the Character's Clips with
ozz-animation's sampling and blending: its stance's blend space at its velocity,
each Clip synchronised by the Stride between its Foot contacts, the stance a
change of stance left blended in by its progress, its Aim offset and ADS grip
added on top, onto a root its position and facing place, as a pure function of
replicated state. Then the rifle held in that pose: its Grip on the weapon
socket of the right hand, and the left arm bent by ozz-animation's two-bone IK
so the left hand reaches the rifle's Fore-end.

A Character's Clips, loaded from its verified pack with ozz-animation's runtime
alone: its ozz-animation Skeleton, mapped Joint by Joint to its Skeleton, each
Clip the Hitbox pose uses by role, with its stride length and Foot contacts, its
stance transition durations and its weapon socket, mapped to its Skeleton's
Joints; refused, naming the part and why, when one is missing or does not load.
A stance's blend space over the body's velocity: the Clips that make its pose
there and their weights. And what a body's Movement is paced by on both sides:
the stride length its Stride advances by there, and the Character's transition
durations.

## Not its responsibility

- Reading a pack's entries, and loading a Skeleton and binding each Hitbox to
  its Joint: assets.
- Placing a Hitbox by its Joint's transform, and where a Hitbox pose puts a
  Character's root: gameplay (`augusta/hitbox.h`).
- Which players are posed and when, the Hitbox history and judging a round:
  simulation, which poses every player each tick.
- Loading a Character's Hitboxes for the Hitboxes Debug overlay: the client, in
  a Development build only (ADR-0074), its one other consumer. It places them in
  the Character's root space by the Rest pose once, when the character loads; no
  client World poses a player.
- Refusing to start on a Character whose Clips do not load: scenario.
- A Cosmetic layer's Clips, and drawing a posed Character: presentation, once
  skinning arrives (ADR-0066). Presentation already gives what a remote player
  would be posed from, its interpolated state, but no client poses one yet.

## Decisions

[ADR-0023](../../../docs/adr/0023-simulationworld-phase-pipeline.md),
[ADR-0040](../../../docs/adr/0040-character-authoring-format.md),
[ADR-0061](../../../docs/adr/0061-skeletal-hitboxes.md),
[ADR-0066](../../../docs/adr/0066-character-animation.md).
