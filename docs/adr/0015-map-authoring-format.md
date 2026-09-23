# Map/Level Authoring Format: OpenUSD

Maps are authored in OpenUSD, used purely as an offline authoring/interchange format, then baked at build time into the engine's own lightweight runtime level format. OpenUSD, Hydra, and their toolchain are never linked into shipped client or server binaries.

A map is authored as its own folder under `<assets-root>/authoring/maps/`, always named `map.usda` regardless of the folder's own name. The fixed name means renaming a map never means renaming the file inside it. The cooker is told a **scenario** folder directly, as an ordinary path (ADR-0030); it refuses a scenario outside `authoring/`.

(Superseded by ADR-0041: a scenario is no longer this same folder holding both the stage and its Lua scripts together - it's its own `authoring/scenarios/<name>/` folder with a `manifest.yaml` naming which map, among others, it composes. What's below about the stage itself - `map.usda`'s fixed name, the Composer/optimize/validate toolchain - is unaffected.)

Meshes are modeled in the DCC of choice and exported to USD; NVIDIA Omniverse USD Composer is used only for scene assembly (placing/referencing meshes, lighting) and PhysX authoring/live debugging (colliders, joints) on the resulting stage — not for modeling. Before baking, the stage is cleaned up with usd-optimize (Apache 2.0; dedups instanced geometry, flattens redundant hierarchy, removes degenerate geometry) and checked with usd-validation-nvidia (Apache 2.0 + CC-BY-4.0; usdchecker-derived validation with additional rules and automatic issue fixing), both offline/build-time only, same non-shipping constraint as OpenUSD itself.

## Considered Options

This follows the industry pattern (Remedy Northlight, Polyphony Digital) of USD-for-authoring → custom-runtime-format, rather than shipping USD/Hydra at runtime.
