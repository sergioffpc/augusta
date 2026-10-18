# Rendering pipeline

The Renderer draws through NVRHI on Vulkan (ADR-0059), but nothing yet says how
the world is lit and shaded: today each mesh has one flat colour under a fixed
light, and the cooked textures are never sampled. This ADR decides the frame,
the Look, the lighting, the image and the hardware they need. It takes effect in
the change that implements it, which amends ADR-0017, ADR-0031, ADR-0032,
ADR-0034, ADR-0058 and ADR-0059 to match.

**Hardware.** The reference is an NVIDIA GeForce RTX 5070 Ti with 16 GB, on
which a Desktop client holds 1440p at 144 fps or more (NFR-17) and a VR client
the headset's refresh rate (NFR-16). The minimum is any Vulkan 1.3 GPU with
`VK_KHR_acceleration_structure` and `VK_KHR_ray_query`, from any vendor; the
Renderer refuses one without them when it is constructed, with an error that
says so (ADR-0033). There is no raster-only path.

**Deferred, as a fixed list of passes.** The frame is a list of passes in code,
in order, each declaring the resources it reads and writes; there is no render
graph:

```text
skinning (ADR-0066) -> G-buffer -> decals -> lighting (ray queries)
  -> translucent forward -> bloom, exposure, tonemap -> upscaler -> HUD
```

The G-buffer holds the opaque and alpha-tested surfaces, with a motion vector
per pixel for the upscaler and the denoiser. A skinned body keeps its previous
frame's vertices and each node its previous transform, so motion is exact. In VR
the targets are arrays of two layers: raster passes draw both eyes with
multiview (ADR-0063), compute passes run once per layer. Desktop takes the same
path with one layer.

**One Look model.** A Look is `UsdPreviewSurface` metallic-roughness, authored
in Composer and shared by Maps, Characters and Props: `diffuseColor`,
`metallic`, `roughness`, `normal`, `occlusion`, `emissiveColor` and `opacity`,
each a constant or a `UsdUVTexture`, shaded with GGX and Lambert. The cooker
refuses MDL, any other shader and any input outside these, naming the prim, and
no longer reads `displayColor`. A texture's BC format follows from the input it
feeds (ADR-0017), so `augusta:textureFormat` goes. A mesh's `UsdGeomSubset`s
each bind a Look, cooked as index ranges of one mesh blob; a mesh with a
textured Look and no UVs is refused, and a Look with `normal` makes the cooker
generate MikkTSpace tangents. Looks ship in the client pack alone (ADR-0019). A
Look and a Surface material are authored apart and neither decides the other
(ADR-0065).

**One shader, bindless, built at start.** One G-buffer shader draws every Look,
with variants only for opaque or alpha-tested and static or skinned. Textures
are bindless through descriptor indexing, indexed by the Look. Every pipeline is
created at start, so no frame stalls compiling one. Commands are recorded on the
Main/Render thread alone (ADR-0005).

**Translucency.** `opacity` below its cutoff discards in the G-buffer, for
fences and foliage. A surface is translucent only when its Look is marked so; it
is drawn in a forward pass after lighting, lit by the same lights and shadows,
with no refraction. Tracers and the particles of ADR-0045 are drawn there too,
and emissive particles light nothing.

**Static light, traced every frame.** A Map's light is fixed, authored in
Composer with UsdLux: a sun (`DistantLight`), a sky (`DomeLight` with an HDRI)
that is also the reflections' and the indirect light's environment, and at most
64 local lights (`SphereLight`, `RectLight`, `DiskLight`), each with an authored
range; the cooker refuses a Map with more, naming it. Intensities are physical
units (`intensity × 2^exposure`). There is no time of day, no light turned on or
off, no fog, no dynamic cloud and no atmospheric scattering. A muzzle flash is a
local light for a few frames, outside the cap.

All lighting is ray traced, by ray queries in compute passes, with nothing baked
by the cooker:

- **Shadows**: a shadow ray per light per pixel, the local lights culled by
  screen tile, with penumbrae from each light's size.
- **Reflections**: traced on glossy Looks, falling back to the sky.
- **Indirect light**: a grid of probes updated by rays every frame, so Props,
  ragdolls and bodies take and change the bounce light; ambient occlusion comes
  from it, not from a pass of its own.
- **Denoising**: NVIDIA's NRD, which runs on every vendor, the only denoiser.

**Acceleration structures.** Each mesh gets a bottom-level structure when the
pack loads, not when it is cooked, since its layout is the driver's. Each drawn
skinned body has its own, refitted every frame from the skinning pass's
vertices. The top-level structure is rebuilt every frame from the Map, Props,
players, Cosmetic bodies and ragdolls, whether in view or not, since off-screen
shadows and reflections count. The local player's head, hidden from its own
camera (ADR-0066), stays in it, so it still casts a shadow and shows in a
reflection; only primary visibility skips it.

**Decals.** Impact decals (ADR-0065) are written into the G-buffer before
lighting, changing its colour, roughness and normal.

**The image.** Rendered in linear HDR, exposed automatically from a histogram so
going from indoors to outdoors adapts, tonemapped with AgX to SDR. Bloom is the
only other effect: no motion blur, depth of field (ADS included), chromatic
aberration, film grain or lens flare, for realism, VR comfort and so no target
is hidden by an effect.

**Upscaling is the anti-aliasing.** One `Upscaler` interface: DLSS Super
Resolution, or DLAA at native resolution, on NVIDIA GPUs; FSR 3.1 on every
other. There is no frame generation, which adds latency, and no DLSS Ray
Reconstruction, so the light is NRD's on every GPU. DLSS comes from NVIDIA's
SDK, a redistributable binary the client's package ships (ADR-0058); a build
without the SDK has FSR alone, as a build without Aftermath has no crash dumps.

**Few settings.** A player chooses resolution and window mode, the upscaler's
preset (from native to performance), vsync or a frame cap, and brightness, in
the client config (ADR-0034); in VR the resolution is the headset's. Shadows,
indirect light, reflections and the image's effects have no quality setting, so
every player sees the same shadows and none can turn off the one that gives an
enemy away.

**Culling.** Nodes are culled against the view's frustum on the CPU; there is no
occlusion culling. Back faces are culled, with winding checked end to end.

**Memory, measured not capped.** Textures are not streamed: a client pack goes
to the GPU whole while loading, before the player is Ready. The cooker writes
into the pack the GPU memory its contents need (textures, meshes and their
acceleration structures, estimated) and prints it at the end of a cook;
`augusta-inspect` shows it too. When it loads a pack the client adds its own
targets at its resolution and compares the sum with the GPU's local memory,
refusing a pack that does not fit with an error stating both. No scenario is
refused for its size.

**Debug views.** An option in the client config, absent from release builds,
shows one channel instead of the image: albedo, normals, roughness, indirect
light, shadows or motion.

**Tested on lavapipe.** Mesa's lavapipe supports acceleration structures and ray
queries, so the smoke test (ADR-0059) drives every pass on CI's runners and
asserts outcomes with pixel probes: the face toward the sun is brighter than the
one away from it, a point in a shadow darker than one out of it, a texture
shows, a metallic Look reflects the sky. Each pass brings its probe; no test
compares whole images, which differ between drivers.

## Consequences

- **GPUs without hardware ray tracing cannot play**: GTX cards and older, RX
  5000 and older.
- **The cooker grows a Look stage**: reading `UsdPreviewSurface`, tangents,
  index ranges per Look and the memory estimate.
- **The client package gains NVIDIA's DLSS and AMD's FSR libraries**, and the
  SBOM with them.
- **Light is authored, never baked**: a Map's look changes when its UsdLux
  changes, with no bake step between.

## Considered Options

- **Clustered forward**: rejected for deferred, which decouples ray-traced
  lighting from the Looks and is what NRD, DLSS and FSR expect, at the price of
  MSAA and of translucency drawn apart.
- **Visibility buffer**: rejected. It is the newest approach, but complex for
  one Map and 8 players.
- **Raster only, with baked light**: rejected. It needs a light baker in the
  cooker, its indirect light ignores moving Props and bodies, and shadow maps
  leak and alias.
- **Ray tracing pipelines with a shader binding table**: rejected. Inline ray
  queries fit compute passes and one Look model needs no per-material hit
  shaders.
- **RTXDI for many lights**: rejected. Another SDK for thousands of lights a Map
  never has.
- **DLSS Ray Reconstruction**: rejected. The light would differ between vendors.
- **A render graph**: rejected. Automatic barriers and pass culling earn little
  with one fixed list of passes.
- **Quality presets**: rejected. Turning shadows down would be a competitive
  advantage.
- **A GPU memory cap per scenario**: rejected. The client checks what it has, so
  a scenario is limited by its players' GPUs, not by a guess.
