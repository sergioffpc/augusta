# Runtime Scene Graph Format

A pack (ADR-0031) needs one more blob type beyond mesh/texture/audio/
collision: a scene graph tying everything together into an actual level,
addressed the same way as any other blob (`AssetType::kScene`).

**Layout:** a flat array of nodes, written in parent-before-child order (so
`parent_index` is always less than the node's own index; the root's
`parent_index` is a sentinel). Each node carries a name (its sanitized USD
prim path, ADR-0031), a **local** transform (translation/rotation/scale),
and a set of optional references — by pack-relative path, same addressing
every other blob type already uses — to a mesh, material, collider,
spawn-point marker, or hitbox, plus free-form string-keyed properties (e.g.
a `script` property pointing at a Lua behaviour, ADR-0022). World transform
is never stored: it's cheap to recompute by walking the parent chain at
load time, and storing it would let it go stale relative to the local
transform it's derived from.

**Coordinate convention:** Y-up, right-handed, 1 meter per unit — extends
ARCHITECTURE.md §8's existing "1 engine unit = 1 meter" statement with the
up-axis/handedness half it left unstated. Matches PhysX's default up axis,
so collider/joint authoring in Composer (ADR-0015) needs no extra
conversion. The cooker reads each authored stage's own `upAxis`/
`metersPerUnit` stage metadata and normalizes into this convention at cook
time (change-of-basis on transforms and points, winding-order fix on
indices when handedness flips) — the runtime format is always in this
convention; nothing downstream (physics, renderer, gameplay) ever branches
on how a given stage happened to be authored.

**Instancing:** USD instanceable prototypes are de-instanced at cook time —
each instance becomes its own node subtree in the flat array, referencing
the shared underlying mesh blob path. Mesh data is therefore already
deduplicated (path-based blob addressing, ADR-0031), even though each
instance gets its own node entry.

**Spawn points and hitboxes** have no native USD prim type. Authoring
convention: a custom bool attribute (`augusta:spawnPoint`, `augusta:hitbox`)
applied on a prim alongside PhysX Collision API schemas (colliders/joints
authored in Composer, ADR-0015). The cooker reads these attributes directly
rather than inferring intent from geometry shape or prim naming.

## Considered Options

- **Storing world transforms instead of local:** rejected — doubles the
  per-node transform cost for a value the loader can recompute in one
  parent-chain walk, and risks the stored value silently drifting from the
  local transform it should be derived from.
- **Client/server pack split (ADR-0019) for the scene blob** — built in
  issue #51: the cooker emits two scene blobs from the same traversal, one
  per pack. Collision/spawn-point/hitbox references are populated in both;
  the server's copy has its mesh/material references stripped instead
  (the reverse of what this section originally proposed - stripping
  collision/spawn-point/hitbox from the client - since ADR-0019/ADR-0031
  settled on shipping that data to both packs, not server-only).
- **True runtime GPU instancing**: deferred. De-instancing at cook time is
  simpler and sufficient until instance count becomes a real performance
  problem; revisit if it does.
- **An authored stable ID for spawn points/hitboxes** (rather than a bool
  attribute): rejected for the same reason ADR-0031 rejected an authored
  asset-name attribute for general addressing — it's an authoring
  convention someone has to maintain for a problem (prim rename breaking a
  hardcoded reference) that hasn't occurred in practice yet.
