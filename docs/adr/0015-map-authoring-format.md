# Map/Level Authoring Format: OpenUSD

Maps are authored in OpenUSD, used purely as an offline authoring/interchange format, then baked at build time into the engine's own lightweight runtime level format. OpenUSD, Hydra, and their toolchain are never linked into shipped client or server binaries.

A map is one USD stage, a file under `<assets-root>/authoring/maps/` (e.g. `maps/augusta.usda`), which a scenario's manifest names by that file (ADR-0041). Files the stage composes (sublayers, textures) may sit in a folder beside it. A map holds no scripts and names no scenario, so one map can be composed into several: the cooker is told a **scenario** by its name, which resolves to `authoring/scenarios/<name>.yaml` (ADR-0030, ADR-0041).

Meshes are modeled in the DCC of choice and exported to USD; NVIDIA Omniverse USD Composer is used only for scene assembly (placing/referencing meshes, lighting) and PhysX authoring/live debugging (colliders, joints) on the resulting stage — not for modeling. Before baking, the stage is cleaned up with usd-optimize (Apache 2.0; dedups instanced geometry, flattens redundant hierarchy, removes degenerate geometry) and checked with usd-validation-nvidia (Apache 2.0 + CC-BY-4.0; usdchecker-derived validation with additional rules and automatic issue fixing), both offline/build-time only, same non-shipping constraint as OpenUSD itself.

## Considered Options

This follows the industry pattern (Remedy Northlight, Polyphony Digital) of USD-for-authoring → custom-runtime-format, rather than shipping USD/Hydra at runtime.
