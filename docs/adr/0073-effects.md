# Effects: one catalogue of particles, decals and brief lights

ADR-0045 gave particles to the renderer and ADR-0065's Impact facet names
particles and a decal for each Surface material, but nothing says how one is
authored: OpenUSD has no particle schema. Explosions (ADR-0070), impacts and the
Muzzle (ADR-0064) all need them. This ADR decides how an Effect is authored and
bounded. It takes effect in the change that brings the first Effect, which
amends ADR-0031, ADR-0065 and ADR-0067 to match.

**One catalogue, by name.** `authoring/effects.yaml` at the assets root, beside
the Surface materials' catalogue, holds every Effect by name: its emitters
(sprites, colours, sizes, lifetimes, velocities, whether they collide against
the depth buffer), an optional decal and an optional brief light. It is cooked
into the client pack only. A Surface material's Impact facet, an Explosion kind
and the Muzzle name an Effect; a name the catalogue lacks fails the cook, naming
where it is used.

**An explosion as the player sees it.** An Explosion's Effect gives a flash, a
local light for a few frames outside ADR-0067's cap like a muzzle flash, fire,
smoke and dust; the Surface material it went off on adds its own impact and
debris, and a scorch decal is left. Its `explosion` cue is spatialised by Steam
Audio (ADR-0010). Nothing shakes the camera, on a desktop or in VR.

**Fixed bounds, no setting.** Each client keeps at most a fixed number of live
particles and decals, the same for every player; past it the oldest decal goes
and new particles of the lowest priority are not born. The bounds come from a
benchmark on the reference GPU (ADR-0013), and no setting changes them, as none
changes the shadows (ADR-0067).

## Considered Options

- **Effects authored in USD**: rejected. OpenUSD has no particle schema, and one
  of our own would be a YAML catalogue in USD's syntax.
- **A particle quality setting**: rejected. A player who turns smoke and dust
  down sees what others cannot.
- **Camera shake**: rejected. It sickens in VR and hides targets on a desktop.
