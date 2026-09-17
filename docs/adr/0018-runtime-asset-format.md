# Runtime Asset Format

Runtime assets ship as a single signed, verified pack file per target, content-hashed with BLAKE3 and signed with Ed25519. Assets are addressed by relative path within the pack — there's no cross-pack GUID/manifest indirection layer at this scale (the pack's own internal table of contents, needed to resolve those relative paths, is an implementation detail, not a separate addressing system).

See ADR-0031 for the container's actual byte layout and how the cooker (ADR-0030) populates it from the authored OpenUSD stage.
