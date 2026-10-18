# Breakables and Integrity: the Map never breaks

The game wants walls blown open and things shot to pieces. A Map's collision is
cooked into both packs, its Probe volumes are baked (ADR-0010), each mesh's
acceleration structure is built when the pack loads (ADR-0067), and penetration
measures a surface's thickness against it (ADR-0065): all of them assume the Map
does not change. This ADR keeps that assumption. It takes effect in the change
that brings the first Breakable, which amends ADR-0015, ADR-0039 and ADR-0045 to
match.

**What breaks is a Prop.** A **Breakable Prop kind** is authored in Composer
already broken: its whole shape, and the pieces it becomes. When one of its
Props breaks, the server removes it and spawns its large pieces as Props, each
with the whole's velocity and an outward push, reliably like any spawn
(ADR-0045). Its small pieces are Cosmetic bodies each client spawns from its
Surface material's Impact facet. A wall that can be blown open is a Breakable
Prop placed in the Map; replication, the Match capture and Replay take it as
they take every Prop.

**One Integrity for breaking and exploding.** A Breakable or explosive Prop kind
has an Integrity, a Parameter (ADR-0039) per Prop kind. A round takes from it by
the energy it lost in the Prop (ADR-0065), an Explosion by its damage, a knock
by its impulse above a threshold. At zero a Breakable breaks, an explosive Prop
explodes (ADR-0070), and one that is both does both. An Explosion an Explosion
sets off happens on the next Tick, so a chain spreads one Tick a link and keeps
each Tick's work bounded. The Prop kind authors the shape: its pieces and which
Explosion it makes; the Parameters hold the numbers.

**Sound around a Breakable.** Probe volumes are baked without Breakables, so
their reflections and paths are those of the opening, open or not. An intact
Breakable's Acoustic geometry, an instanced mesh that moves with it (ADR-0045),
occludes and transmits the direct sound; pathing validates each baked path
against the live scene and finds an alternate one, so a closed Breakable does
not let a path through it play. When it breaks its geometry goes, and its pieces
carry none. Baked reverb never changes: a closed Breakable only drops the probes
beyond it from the listener's mix, and the reverb of the room beyond it leaks
whole, an accepted cost. Steam Audio takes a change at its next scene commit,
which waits for its simulation thread, so a broken wall leaves the direct sound
up to one of that thread's periods late. Which scene layout keeps those commits
cheap is measured before the first Breakable lands.

## Consequences

- **Blowing open a wall spawns many Props at once**, against ADR-0045's cap on
  awake Props; a Breakable's piece count is bounded by the cooker.
- **The Map stays a fixed thing**: its collision, the renderer's acceleration
  structures and its Probe volumes are never rebuilt during a Match. Steam
  Audio's own structure over the Map's Acoustic geometry may be, depending on
  the scene's layout.
- **Pathing costs more**: validation and alternate paths run every period.

## Considered Options

- **Breaking the Map at runtime** (Voronoi or voxel fracture): rejected. It
  rebuilds collision, acoustic paths and acceleration structures during a Match,
  on both sides, and makes the Map a replicated thing.
- **Health on every Prop**: rejected. A crate that cannot break has nothing to
  wear down.
- **Integrity authored in the Prop kind**: rejected. It is a number tuned for
  play, like damage, and the Parameters are where those are tuned.
