# Runtime Asset Format

Runtime assets ship as a single signed, verified pack file per target, content-hashed with BLAKE3 and signed with Ed25519. Assets are addressed by relative path within the pack — there's no cross-pack GUID/manifest indirection layer at this scale (the pack's own internal table of contents, needed to resolve those relative paths, is an implementation detail, not a separate addressing system).
