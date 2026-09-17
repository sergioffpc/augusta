# Mesh Import & Optimization

Meshes are read directly from the authored OpenUSD stage (ADR-0015) via the OpenUSD API — no separate multi-format import step, since authoring is USD end-to-end — and optimized via meshoptimizer (vertex cache optimization, simplification, quantization).

Non-USD source assets (glTF/GLB, FBX, OBJ) are brought in via Adobe's USD-Fileformat-plugins (Apache 2.0) rather than a separate importer: these register as OpenUSD `SdfFileFormat` plugins, so a source file composes directly as a USD layer through the same OpenUSD API above — no parallel import path or intermediate in-memory scene representation.

## Considered Options

Assimp was considered and rejected: it produces its own in-memory scene graph independent of USD, which would mean maintaining two import paths (USD-native and Assimp) instead of one. The Adobe plugins avoid that by extending OpenUSD's own file-format resolution instead of importing outside it.
