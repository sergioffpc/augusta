# Map/Level Authoring Format: OpenUSD

Maps are authored in OpenUSD, used purely as an offline authoring/interchange format, then baked at build time into the engine's own lightweight runtime level format. OpenUSD, Hydra, and their toolchain are never linked into shipped client or server binaries.

## Considered Options

This follows the industry pattern (Remedy Northlight, Polyphony Digital) of USD-for-authoring → custom-runtime-format, rather than shipping USD/Hydra at runtime.
