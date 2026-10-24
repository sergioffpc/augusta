# assets

`augusta_assets`. Shared: linked by both the client and the server.

## Responsibility

Loads the runtime packs the cooker produces and resolves their content by
pack-relative path. It verifies each pack's BLAKE3 hash and Ed25519 signature
before trusting anything in it, memory-maps the file once and decodes its
content straight out of it, and reads a pack's layout without a key for the
tools that show one. It owns the pack's wire format.

## Not its responsibility

- Cooking, inspecting and verifying packs: `tools/pack`, which walks the
  authored stages and links this module, through its native module, to write,
  hash and sign packs, and to read and verify them.
- Building a map's collision from a pack: physics (`augusta/map.h`).
- Reading the ozz-animation archives of a Character's Skeleton and Clips, which
  a pack carries without reading: animation, through ozz-animation's runtime.
- Playing the sounds a pack holds: audio.

## Decisions

[ADR-0018](../../../docs/adr/0018-runtime-asset-format.md),
[ADR-0030](../../../docs/adr/0030-asset-cooking-pipeline.md),
[ADR-0031](../../../docs/adr/0031-pack-container-format.md).
