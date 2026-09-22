# Pack Container Format

Defines the on-disk byte layout of the pack file ADR-0018 describes, and how the cooker (ADR-0030) turns the cleaned/validated OpenUSD stage into it.

**Conversion, per USD prim:**
- `UsdGeomMesh` prims: points/normals/UVs/indices read via the OpenUSD API (ADR-0016), optimized with meshoptimizer, written as one mesh blob.
- Textures referenced by `UsdShadeMaterial`/`UsdUVTexture`: compressed to BC7/BC5/BC4 DDS via DirectXTex (ADR-0017), one blob each.
- Lua scripts (ADR-0039): every `*.lua` file under the scenario's folder, stored as its text with no framing, one blob each. Written to the server pack only (ADR-0019): a client is sent the values a script decides, never the script.
- Audio: mono PCM per ADR-0020. How an audio clip is associated with a USD prim (vs. referenced from elsewhere, e.g. gameplay Lua) isn't decided yet — out of scope here, needed before the cooker can actually emit audio blobs.
- Collision geometry, spawn points, hitboxes: read from the PhysX-authored USD data (colliders/joints from Composer, ADR-0015), serialized with the existing custom binary format (ADR-0007) rather than a new one. Written to both the client and server pack (ADR-0019) - unlike mesh/texture content, which is client-pack-only, the client needs this data too (e.g. client-side prediction, movement, and hit-detection against the same geometry the server uses).

**Addressing:** each blob's pack-relative path is the source USD prim's path, sanitized (leading `/` stripped; `/` kept as the path separator). A script's pack-relative path is its path relative to the scenario's folder, with `/` as the separator (`parameters.lua`, `rules/round.lua`). No separate authored ID — consistent with ADR-0018 already rejecting a GUID/manifest indirection layer. Renaming or moving a prim in the authored stage therefore changes its runtime path; nothing here guards against that.

**File layout**, in write order:
1. **Header** — magic (`"AUGP"`), format version, index offset/count, data section offset. Written first as a placeholder, patched once the index offset is known.
2. **Data section** — every asset blob, back-to-back, in traversal order. Offsets are recorded as each blob is written.
3. **Index** — one entry per blob: type tag (Mesh/Texture/Audio/Collision/SpawnPoint/Hitbox/Scene/Script), path, offset, size. Written after the data section, since it needs the recorded offsets.
4. **Trailer** — BLAKE3 hash of everything from byte 0 through the end of the index, followed by the Ed25519 signature of that hash (ADR-0030's pack → hash → sign order). Appended last.

Verifying a pack means re-hashing everything but the trailer and checking the signature before trusting the header/index at all — the header is otherwise just as untrusted as the data it points into.

## Considered Options

An authored stable identifier (a `augusta:assetName`-style USD attribute, decoupled from prim hierarchy position) was considered for addressing, to survive scene reorganization in Composer. Rejected for now: it adds an authoring convention someone has to maintain and a validation rule to enforce it, for a problem (prim rename fallout) that hasn't actually occurred yet. Revisit if renames start breaking hardcoded gameplay references in practice.
