# Shading Language: Slang

Shaders are authored in Slang (Apache 2.0), an explicit, deliberate choice of
shading language independent of the renderer's API.

## Consequences

Shaders are compiled at build time by `slangc` from vcpkg's `shader-slang`
(ADR-0059), so its version is the one the vcpkg baseline pins (ADR-0025).
