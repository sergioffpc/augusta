# Map/Level Authoring Format: OpenUSD

Maps are authored in OpenUSD, used purely as an offline authoring/interchange format, then baked at build time into the engine's own lightweight runtime level format. OpenUSD, Hydra, and their toolchain are never linked into shipped client or server binaries.

A map is authored as a **scenario**: a folder, conventionally under `authoring/`, holding the stage, always named `map.usda` regardless of the folder's own name, and the Lua scripts that go with it (`authoring/test_map/map.usda`, `authoring/test_map/parameters.lua`, ADR-0039). The fixed name means renaming a scenario never means renaming the file inside it. The cooker is told the folder directly, as an ordinary path, and packs everything under it (ADR-0030).

Meshes are modeled in the DCC of choice and exported to USD; NVIDIA Omniverse USD Composer is used only for scene assembly (placing/referencing meshes, lighting) and PhysX authoring/live debugging (colliders, joints) on the resulting stage — not for modeling. Before baking, the stage is cleaned up with usd-optimize (Apache 2.0; dedups instanced geometry, flattens redundant hierarchy, removes degenerate geometry) and checked with usd-validation-nvidia (Apache 2.0 + CC-BY-4.0; usdchecker-derived validation with additional rules and automatic issue fixing), both offline/build-time only, same non-shipping constraint as OpenUSD itself.

## Considered Options

This follows the industry pattern (Remedy Northlight, Polyphony Digital) of USD-for-authoring → custom-runtime-format, rather than shipping USD/Hydra at runtime.
