# Pack Container Format

Defines the on-disk byte layout of the pack file ADR-0018 describes, and how the
cooker (ADR-0030) turns the cleaned/validated OpenUSD stage into it.

**Conversion, per USD prim:**

- `UsdGeomMesh` prims: points/normals/UVs/indices read via the OpenUSD API
  (ADR-0016), optimized with meshoptimizer, written as one mesh blob.
- Textures referenced by `UsdShadeMaterial`/`UsdUVTexture`: compressed to
  BC7/BC5/BC4 KTX2 via libktx (ADR-0017), one blob each: the KTX2 file as a u32
  byte count, then its bytes, and nothing else. Its block format is read from
  the KTX2 header's `VkFormat`, not stored again beside it.
- Lua scripts (ADR-0022, ADR-0039): each script the scenario's manifest names,
  stored as its text with no framing, one blob each. Written to the server pack
  only (ADR-0019): a client is sent the values a script decides, never the
  script.
- Character list (ADR-0042): the manifest's `characters` names, in manifest
  order, as one blob at the fixed path `Characters` - a count then each name as
  a string, at most 255. Written to both the client and server pack: the server
  checks a join against it, and the client each character it is told about.
- Character eye (ADR-0040): the origin of a character's `Character/Eye` prim, in
  that character's root space, as three floats and nothing else, one blob at
  `<character name>/Character/Eye`. Written to both the client and server pack:
  the client places the local player's camera at it, and the server fires that
  player's Shots from it.
- Character skeleton (ADR-0040, ADR-0061): the UsdSkel `Skeleton` at a
  character's `Character/Skeleton` prim, as one blob at
  `<character name>/Character/Skeleton`: a u32 joint count, at most 128, then
  each joint, parents before children, as its name (a string: its path within
  the skeleton), its parent's index (u32, `0xFFFFFFFF` for a root) and its rest
  transform relative to that parent as a scene node's transform is stored.
  Written to both the client and server pack, the same in each.
- Rifle (ADR-0064): the Grip, Muzzle and Sight children of the default prim of
  the stage the manifest names as its `rifle`, each its frame in the rifle's
  space as a translation and a rotation, stored as a spawn point's are, one blob
  at the fixed path `Rifle` in that order. No rifle mesh is written. Written to
  both the client and server pack: the server places every Muzzle from it.
- Cue sounds (ADR-0020): an audio clip is associated with a client cue, not with
  a USD prim or a script. The cue catalogue is fixed in code, in the client and
  the cooker alike: gunshot, hit marker, hit taken, death, match won, match
  lost. The scenario's manifest names each cue's sound, its variants and its
  gain (ADR-0041). Each cue's sound is one small blob at `sounds/<cue>` (e.g.
  `sounds/gunshot`): its gain in decibels (f32), then its variants' paths (a u32
  count, then each as a string). Each variant is one audio blob at
  `sounds/<cue>/<n>`, numbered from 0 in the manifest's order: its sample rate
  (u32), bits per sample (u8, 8/16/24/32) and its samples as the WAV held them
  (a u32 byte count, then the bytes). The prefix `sounds` is a constant the
  client and the cooker share, so the client finds every cue's sound knowing
  nothing but the catalogue; no blob names it. Written to the client pack only:
  the headless server plays nothing (ADR-0019). The cooker refuses a file that
  is not a mono PCM WAV, naming it, and a manifest lacking any cue, or a cue
  with no variant, a missing file or a gain that is not finite, naming the cue;
  the client resolves every cue's sound and every variant at startup, so a
  missing one is found before a Match, not during one.
- Collision geometry, spawn points, hitboxes: read from the PhysX-authored USD
  data (colliders/joints from Composer, ADR-0015), serialized in the pack's own
  binary format of fixed-width fields, not the protocol's Protocol Buffers
  (ADR-0007): a pack's contents are never messages. Written to both the client
  and server pack (ADR-0019) - unlike mesh/texture content, which is
  client-pack-only, the client needs this data too (e.g. client-side prediction
  of movement against the same geometry the server uses, and drawing where a
  Shot lands, ADR-0044; hit detection itself runs only on the server, ADR-0024).
- Prop kinds and the Props placed in the Map (ADR-0045): each Prop kind's stage,
  with PhysX's rigid-body and mass schemas, read into its colliders, mass and
  Surface material and serialized in the pack's same binary format, and each
  placed Prop as its Prop kind and pose. Written to both the client and server
  pack: the server simulates them and the client moves them to the poses it is
  sent and predicts against them. The Prop kind's mesh is client-pack-only, like
  any mesh.
- The Surface material catalogue (ADR-0065): its ballistic and contact facets to
  both packs, its acoustic and impact facets to the client pack only. A physics
  material bound to a prim is refused: a surface's friction and restitution come
  from its Surface material alone.
- Ragdolls (ADR-0045): a Character's ragdoll bodies and joints, authored in
  Composer, serialized in the same format. Written to the client pack only:
  ragdolls are Cosmetic bodies the server never simulates.

**Addressing:** each blob's pack-relative path is the source USD prim's path,
sanitized (leading `/` stripped; `/` kept as the path separator). A script's
pack-relative path is the fixed name of the role the scenario's manifest gives
it (`parameters.lua`, `rules.lua`, ADR-0041), whatever its authored file is
called, so the server finds each knowing nothing but its role. No separate
authored ID — consistent with ADR-0018 already rejecting a GUID/manifest
indirection layer. Renaming or moving a prim in the authored stage therefore
changes its runtime path; nothing here guards against that.

**File layout**, in write order:

1. **Header** — magic (`"AUGP"`), format version, index offset/count, data
   section offset, then a flags byte and the client pack hash (ADR-0038): the
   BLAKE3 hash of the client pack cooked in the same run, its trailer's 32
   bytes. Only a server pack names one, with the flag set; a client pack has the
   flag clear and 32 zero bytes, and a header with any other flag, or a hash it
   does not flag, does not load. The server admits only a client whose Join
   request names that hash, so a client never plays against a server pack cooked
   from other content, even one signed by the same key. Written first as a
   placeholder, patched once the index offset is known.
2. **Data section** — every asset blob, back-to-back, in traversal order.
   Offsets are recorded as each blob is written.
3. **Index** — one entry per blob: type tag
   (Mesh/Texture/Audio/Collision/SpawnPoint/Hitbox/Prop/Ragdoll/Scene/Script/Characters/Eye/CueSound/Skeleton/Rifle),
   path, offset, size. Written after the data section, since it needs the
   recorded offsets.
4. **Trailer** — BLAKE3 hash of everything from byte 0 through the end of the
   index, followed by the Ed25519 signature of that hash (ADR-0030's pack → hash
   → sign order). Appended last.

Verifying a pack means re-hashing everything but the trailer and checking the
signature before trusting the header/index at all — the header is otherwise just
as untrusted as the data it points into. Once verified, the trailer's hash names
that pack: two cooks of different content never share it, which is what the
header's client pack hash relies on.

**Format version:** the engine loads packs of its own format version alone
(`kFormatVersion`), and refuses any other at load. Each server an Environment
runs records, beside its `packVersion`, the `packFormat` its pack was cooked in,
as `augusta-publish` reads it from the published server pack's header, and CI
fails a pull request after which any `packFormat` differs from the engine's, or
a `packVersion` has none. A change to the format therefore needs every
Environment's packs re-cooked in it and published before it merges, and the pull
request that makes it moves each Environment's `packVersion` and `packFormat` to
them. `staging` runs the last release tag's binaries but takes its values from
`develop`, so until that change is tagged its new pod refuses the new pack and
its old pod serves on. The version guards what an Environment serves: a change
to blobs only the client pack holds, which no Environment publishes, leaves it
as it is, and a client given a client pack of another layout fails its startup
load, naming the blob it could not resolve.

## Considered Options

The client pack hash as one more blob in the index, at a fixed path with a type
tag of its own, was considered and rejected: it is not content any prim or
script was cooked into but a fact about the pack itself, which pack it pairs
with, so it belongs with the other facts about the pack in the header, read once
by Load rather than resolved by path like an asset.

An authored stable identifier (a `augusta:assetName`-style USD attribute,
decoupled from prim hierarchy position) was considered for addressing, to
survive scene reorganization in Composer. Rejected for now: it adds an authoring
convention someone has to maintain and a validation rule to enforce it, for a
problem (prim rename fallout) that hasn't actually occurred yet. Revisit if
renames start breaking hardcoded gameplay references in practice.
