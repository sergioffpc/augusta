# Asset Cooking Pipeline

The asset cooker (`tools/asset-cooking/`, target `augusta_asset_cooking`) runs as a single ordered pipeline: **usd-optimize → usd-validation-nvidia → DirectXTex (texconv) → meshoptimizer → pack → hash → sign**. Stage cleanup and validation (ADR-0015) happen first, against the raw authored OpenUSD stage, since everything downstream reads from the cleaned/validated stage rather than the authored one. Texture compression (ADR-0017) and mesh optimization (ADR-0016) are independent of each other — no cross-asset-type dependency — and run in that relative order only because the pipeline is sequential today; nothing here prevents parallelizing them later.

Packing, hashing, and signing run last, and specifically in that order: the client and server packs (ADR-0019) are assembled first (contents + internal table of contents, ADR-0018), then each assembled pack is content-hashed with BLAKE3, then that hash is signed with Ed25519. Hash and signature are computed over the finished pack, not per individual asset — matching ADR-0018's "a single signed, verified pack file per target, content-hashed... and signed", which describes a pack-level guarantee, not a per-asset one. A corollary: the cooker cannot emit a partial/streaming pack, since hashing/signing require the full assembled contents to already exist.

See ADR-0031 for the pack's actual byte layout (header/index/trailer) and the per-USD-prim conversion mapping.

## Considered Options

Hashing each cooked asset individually (embedding per-asset hashes in the pack's index, in addition to the pack-level hash/signature) was considered, for finer-grained corruption detection than an all-or-nothing pack check. Deferred, not rejected outright: it's a compatible future addition to the pack's index format (ADR-0018 already treats the index as an implementation detail) that doesn't change this pipeline's stage order, so it's left out until a concrete need (e.g., partial pack streaming) justifies the added format complexity.
