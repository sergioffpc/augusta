# Surface materials, penetration and ricochet

A round stops on the first surface or player it meets (ADR-0002, ADR-0044),
whatever that surface is: a pane of glass stops it as a concrete wall does.
Sound already tells surfaces apart by their Acoustic material (ADR-0062), and
ADR-0045 left penetration by material and thickness to a separate decision. This
ADR gives every surface one Surface material, decides what a round does when it
meets one, and how much damage a round that went through something still does.
It takes effect in the change that brings penetration, which amends ADR-0002
(the bound on a flight), ADR-0039 (the ammo's penetration), ADR-0041 (the
manifest's Prop kinds) and ADR-0044 (a flight that goes on past a player) to
match.

**One Surface material per surface.** A surface names its Surface material with
`augusta:material`: a Map's or a Prop kind's collider, a piece of Acoustic
geometry, and each Hitbox of a Character. The name is an entry of one catalogue,
`authoring/materials.yaml` at the assets root, shared by every Map and every
scenario. An entry says what its material does to everything, in four facets:

- **Acoustic**: Steam Audio's absorption, scattering and transmission
  (ADR-0010). Client pack only.
- **Ballistic**: its resistance to penetration, its critical angle, the speed a
  ricochet keeps and its spread. Both packs, since the server flies rounds and
  every client draws them.
- **Contact**: its friction and restitution, which PhysX uses for the bodies
  that touch it. Both packs.
- **Impact**: how a strike on it looks and sounds: particles, a decal, the
  `impact` cue's choice for it (ADR-0062's shape), and optional debris, as
  Cosmetic bodies with a cap per client and a lifetime (ADR-0045). Client pack
  only.

`generic` and `flesh` are required entries. A collider with no material is
`generic`, a Hitbox with none is `flesh`, and Acoustic geometry must name one. A
name the catalogue lacks fails the cook, naming the prim. Friction and
restitution come from the catalogue alone: the cooker refuses a physics material
bound to a prim, so a surface's contact has one source.

**Penetration by energy and thickness.** When a round strikes a surface at an
angle steeper than the material's critical angle, the server measures the
surface's thickness along the round's path, by a query back from beyond it up to
a maximum depth, and takes speed from the round:

```text
v_exit^2 = v^2 - R_material * thickness / P_ammo
```

R is the material's resistance and P the ammo's penetration, a Parameter
(ADR-0039). With nothing left, the round stops in the surface. Otherwise it
leaves the far side at its remaining speed, deviated within the material's
spread.

**Ricochet at a grazing angle.** A round striking a surface at an angle more
grazing than its material's critical angle ricochets instead: it is reflected,
within the material's spread, keeping the fraction of its speed the material
says. Whether it ricochets is a threshold, not a chance. A body never ricochets
a round.

**Rounds go through players.** A player's Hitboxes are a surface like any other,
judged in the Shooter's delay (ADR-0044), with each Hitbox's Surface material
and the length of the round's path inside that player's Hitboxes as the
thickness. A round hits a player at most once, in the first Hitbox it enters,
which gives the Body part, and then flies on with what is left, to the next
player, Prop or the Map. Every player struck gets its own hit, and its shooter
its own Hit confirmation.

**Damage follows the energy left.** A hit's damage is the Parameters' damage for
its Body part and ammo (ADR-0039), times `(v / v_muzzle)^2`, with v the round's
speed as it enters. A round that went through a door does less than one that did
not.

**A Prop a round goes through is pushed by what it lost.** A round that passes
through a Prop pushes it with ADR-0045's impulse for that weapon, scaled by the
fraction of its speed it lost; one that stops in it pushes it with all of it.

**Bounded.** A round passes through or ricochets off a fixed number of surfaces,
players included, before it stops, so a flight's work keeps a bound whatever the
Map, as ADR-0002's flight time keeps it in time.

**Deterministic, so clients draw it too.** Every deviation is drawn from a seed
of the shooter and the Shot's tick, which a player fires at most one round in
(ADR-0002). A client's PresentationWorld runs the same math, with the ballistic
facet of its own pack and the same seed, to draw each segment's tracer and
impact (ADR-0044); the Shot message does not change. Against players and Props a
client's drawing can differ, since it sees them interpolated and not in the
Shooter's delay or where the server has them now; hits and damage stay the
server's.

## Consequences

- **The Acoustic material is no longer named on its own**: ADR-0062's
  `augusta:acousticMaterial` and the acoustic catalogue become
  `augusta:material` and this catalogue's acoustic facet.
- **The golden trajectories gain cases** for penetration and ricochet, which are
  deterministic like the rest of a flight (NFR-03).
- **A Character's Hitboxes carry a material**, so armour later is a material and
  not a new mechanism.

## Considered Options

- **A vocabulary per concern** (acoustic, ballistic, physics materials apart):
  rejected. An author could make a wall concrete to sound and wood to rounds.
- **Penetrable or not, per material**: rejected. A plank and a wall of the same
  wood would stop a round alike.
- **Ricochet by chance**: rejected. A seeded chance is no less deterministic,
  but a player could never learn which angles ricochet.
- **Rounds that stop on the first player**: rejected. A round that goes through
  glass and wood has no reason to stop in a body.
- **Material from the render material**: rejected. A texture says how a surface
  looks, not what it is, and the server has no render materials.
