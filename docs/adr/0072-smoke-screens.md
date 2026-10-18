# Smoke screens: a deterministic flood fill drawn by every client alike

A smoke grenade hides players, so its cloud has to be where it is and as thick
on every client: particles that differ between clients (ADR-0045) would let one
see through what hides it on another. ADR-0067 draws no fog or volume. This ADR
decides how a Smoke screen is shaped and drawn. It takes effect in the change
that brings the smoke grenade, which amends ADR-0031 and ADR-0067 to match.

**The server announces, each client grows it.** The server sends only the smoke
grenade's Explosion (ADR-0070): where and on which Tick. That point is the
cloud's source even if the grenade rolls on. The server keeps no cloud, since it
judges no sight, and rounds pass through it.

**It spreads through the Map, not through walls.** The cooker writes into the
client pack a coarse voxel grid (0.5 m) of the Map's free space, Breakables
counted as free whether open or not. From the source, each client floods that
grid in integer steps, to a volume and over times the Parameters set (ADR-0039),
then thins it until it clears. The same inputs give the same voxels on every
client and platform, so it fills corridors and rooms the same everywhere; Props
do not block it.

**Lit once per voxel, drawn as a volume of its own.** Each frame, a compute pass
lights the grid's occupied voxels once: one shadow ray to the sun per voxel, the
cloud's own shadow by marching the grid toward it, and the ambient light from
ADR-0067's probe grid, not the sky, so smoke indoors takes the room's light and
does not glow. A ray-marching pass between lighting and the translucent forward
pass then marches the lit grid from the camera, its density shaped by noise from
the same seed, with no multiple scattering. A shadow ray per march step was
rejected: a screen full of smoke would cost about ten times every other ray in
the frame. The same pass writes the cloud into a grid sliced by depth, which the
translucent forward pass reads, so a tracer or a particle behind or inside the
smoke is hidden by what lies between it and the camera.

**Stable on its own.** DLSS takes no mask for what moves without motion vectors,
so the march leaves no per-pixel noise for the upscaler to settle: its noise is
fixed in the grid, not in the screen. FSR is given the cloud's opacity as its
reactive mask.

**No quality setting.** Every player sees the same thickness, as every player
sees the same shadows (ADR-0067). A VR client marches at half resolution, fixed
for its Play mode and not a player's choice, since both eyes march and the
headset's frame is short. It is ADR-0067's one exception to drawing no volume.

## Consequences

- **The client pack gains the Map's voxel grid**; the server pack does not.
- **The flood fill has golden tests**, being integer and deterministic.

## Considered Options

- **Particles for the Smoke screen**: rejected. Particles may differ between
  clients, and a thinner one is a window.
- **A growing sphere**: rejected. It draws through walls into rooms the smoke
  never reached.
- **A cloud the server simulates and sends**: rejected. The server judges no
  sight, and the same result comes from a point and a Tick.
