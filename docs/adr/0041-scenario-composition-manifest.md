# Scenario Composition Manifest

A map (ADR-0015) and a character (ADR-0040) are both pure, reusable authoring content now — neither is itself "the thing that gets cooked into one pack." A **scenario** is its own folder, `<assets-root>/authoring/scenarios/<name>/`, holding a manifest always named `manifest.yaml` regardless of the folder's own name — the same fixed-name convention as `map.usda`/`parameters.lua` (ADR-0015) — plus that scenario's `parameters.lua`/`behaviours.lua`/`objectives.lua`. This supersedes ADR-0015's original definition of "scenario" as a folder holding both the stage and the Lua scripts that go with it: the stage and the scripts no longer live in the same folder, and a map authored once can now be composed into more than one scenario.

The manifest is YAML, not Lua, since composing a map and a list of characters is plain data with nothing to compute - no expressions, no derived values the way `parameters.lua`'s stamina numbers are (ADR-0039). YAML is also what this repo already reaches for outside Lua's own niche (ADR-0034's rationale for `augustac.yaml`/`augustad.yaml` - "the repository already keeps its configuration in JSON and YAML... a third format for one more kind of config is one more thing to know"), so the manifest follows that instead of opening a second Lua use case with a different shape than the Parameters sandbox.

```yaml
map: maps/augusta
characters:
  - characters/player
```

`map` names exactly one map; `characters` a list of zero or more characters - both as paths relative to `authoring/`, the same convention ADR-0040 already established for a character's own pack addressing. The cooker resolves these paths, reads the named map's stage, walks the named characters' stages, and packs all of it plus the scenario's own Lua scripts into one client/server pack pair - only what feeds the walk changes; the pack's byte layout (ADR-0031) and the client/server split (ADR-0019) are unaffected.

This supersedes ADR-0040's packing decision: "every cook run walks `authoring/characters/` unconditionally... every scenario pack ships the full character library" is replaced — a scenario pack now ships exactly the characters its manifest names. ADR-0040's own Considered Options already named this alternative ("per-scenario explicit character binding") and deferred it until the always-pack-everything shortcut stopped being good enough; this is that revisit, not a reversal of a mistake.

Implemented: `augustap <name>` (`cli.py`) resolves the name via `scenario.py`'s `resolve_scenario`, reads `manifest.yaml` with PyYAML (`pyproject.toml` now depends on it), and runs usd-optimize/usd-validation-nvidia once per composed stage (the map, then each character) before `cook.py`'s `cook_scenario` walks all of them into one client/server pack pair. A character's own prims are addressed `<manifest path>/<prim path>` (e.g. `characters/player/Character/Visual`) with the prim's local transform and its stage's own up-axis/metersPerUnit correction baked directly into its points, since - unlike a map's top-level nodes - a character contributes no `Scene` node of its own to carry that correction as a transform; the `Scene` blob stays map-only.

`cook.py`'s geometry reader also now classifies `UsdGeomCapsule` (tessellated into a triangle buffer, `_read_capsule_geometry`) alongside `UsdGeomMesh`/`UsdGeomCube` (ADR-0032) - `examples/authoring/characters/player/character.usda`'s `Visual`/`Collider` capsules cook into real mesh/collision entries, addressed `characters/player/Character/Visual`/`.../Collider`.

**Still open**: UsdSkel skinning (ADR-0040's own still-open question) - a capsule is a placeholder collision-derived shape, not an art asset with a rig.

## Considered Options

**A USD reference/payload in `map.usda` naming its characters, instead of a separate manifest**: rejected. It would mean a map authored to be reusable across scenarios - the exact flexibility this ADR's map/scenario split exists to preserve - hard-codes one scenario's character roster into itself. The manifest keeps that decision at the scenario, where ADR-0039 already put every other per-scenario composition choice.

**A Lua manifest (`scenario.lua`), matching `parameters.lua`/`behaviours.lua`/`objectives.lua`'s format**: this ADR's first draft. Rejected on reflection: composition is pure data (a map path, a list of character paths), so Lua's sandboxed-execution machinery (ADR-0039: no `io`/`os`/randomness) buys nothing here, and it would be a second, differently-shaped Lua convention in the same folder next to the Parameters script's own. YAML says the same thing with nothing to sandbox.

**Keep packing every character unconditionally, and let the manifest compose only the map** (drop `characters` from it, change nothing else): considered, since it's the smaller diff from ADR-0040's current text. Rejected once the manifest exists at all - if a scenario already has to declare its map, declaring its characters costs nothing extra and avoids exactly the pack-bloat ADR-0040 flagged as the thing to revisit once it stopped being hypothetical.

**Folding map/character references into `parameters.lua`, rather than a new manifest file**: rejected. `parameters.lua` is the sandboxed Parameters script ADR-0039 already defines a strict schema for ("an unknown or missing key stops the server loading it"); folding composition into it would either loosen that schema or overload one file with two unrelated jobs. A dedicated manifest leaves `parameters.lua`'s contract exactly as ADR-0039 left it.
