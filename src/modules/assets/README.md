# assets

`augusta_assets`. Shared: linked by both the client and the server.

## Responsibility

Loads the runtime packs the cooker produces and resolves their content by
pack-relative path. It verifies each pack's BLAKE3 hash and Ed25519 signature
before trusting anything in it, memory-maps the file once and decodes its
content straight out of it. It owns the pack's wire format.

## Not its responsibility

- Cooking packs: `tools/pack`, which walks the authored stages and links this
  module, through its native module, to write, hash and sign them.
- Building a map's collision from a pack: physics (`augusta/map.h`).
- Playing the sounds a pack holds: audio.

## Decisions

[ADR-0018](../../../docs/adr/0018-runtime-asset-format.md),
[ADR-0030](../../../docs/adr/0030-asset-cooking-pipeline.md),
[ADR-0031](../../../docs/adr/0031-pack-container-format.md).
