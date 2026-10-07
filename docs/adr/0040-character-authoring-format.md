# Character Authoring Format & Packing

A player character is one USD stage, a file under
`<assets-root>/authoring/characters/` (e.g. `characters/soldier.usda`), which a
scenario's manifest names by that file under the character's **name** (ADR-0041,
e.g. `soldier`): what a client asks to play (ADR-0042) and what the character's
blobs are addressed under, so moving the stage or converting it between `.usda`
and `.usdc` never renames the character. The stage's default prim is always
named `Character`, and its visual mesh is the child prim `Visual`: the client
finds a character's mesh at `<character name>/Character/Visual` (e.g.
`soldier/Character/Visual`) knowing nothing about the character but its name,
and the cooker refuses a character stage whose default prim is named otherwise.
The same way, `Character` has a child prim `Eye` whose origin is where the local
player's camera sits relative to the feet (the root's origin), a marker authored
like a map's spawn points (ADR-0032): eye height is part of the character, not a
constant in the client, so a tall character and a short one see from different
heights. The `Eye` is where the character sees from standing; crouching or
prone, the client lowers it in proportion to the body's height in that stance
(the stance collision heights, `physics::StanceHeight`), so the view drops with
the body without the character authoring an eye per stance. The eye is also
where a player's Shots leave from (US-07): the server fires each round from the
shooter's eye, lowered for its stance the same way, so a shot goes where the
view looks. The cooker writes that point, in the character's root space, as an
eye blob at `<character name>/Character/Eye` in both packs, and refuses a
character stage without one; the client reads the eye of the character it plays
and keeps the camera there, following its body, and the server reads the eye of
every character of the scenario at startup, refusing to run on one it could not
fire for. A character is never itself the argument the cooker (`augusta-pack`)
takes; it's shared authoring content scenarios draw on, not scenario content.
This follows CONTEXT.md's own line between the two: "Map" is the static space a
match is played in — collision, spawn points, hitboxes — and a player character
is an ECS entity, not scene content, so it doesn't belong inside the map's stage
alongside the level.

A character is hittable through its **hitboxes**, authored in its own stage
under `Character`: each is geometry (a `UsdGeomMesh`, `Cube` or `Capsule`,
usually a guide so it is never drawn) marked `augusta:hitbox`, and names the
body part it stands for in `augusta:bodyPart`: `head`, `torso` or `limb`. A body
part may have several (two arms and two legs are four `limb` hitboxes). They are
placed relative to the feet, standing, on a character that faces -Z, where a
view of yaw 0 looks; the server lowers them with the stance as the client lowers
the eye, and turns them about the vertical axis by the yaw its body faces
(ADR-0038), so no character authors hitboxes per stance or per facing. Every hit
on a player resolves to a body part (US-11), so the cooker refuses a hitbox
without a body part or with another name, and a character without at least one
hitbox for each of the three. Each hitbox is a blob at its own prim's path under
the character (e.g. `soldier/Character/HeadHitbox`): its body part as one byte,
then its geometry as a mesh blob, in the character's root space. Both packs
carry them, and the server resolves every hitbox under a character's name at
startup, refusing to run on a character it could not judge a hit on.

Characters are authored in USD, same toolchain and rationale as ADR-0016:
Composer for assembly, Adobe's USD-Fileformat-plugins composing any glTF/FBX/OBJ
source as a USD layer rather than a second import path. Mesh geometry follows
the exact `UsdGeomMesh` → meshoptimizer path ADR-0016/ADR-0031 already define.
Skeleton/skinning would be UsdSkel once that pipeline exists; this ADR doesn't
resolve the runtime skeletal-animation format itself (still open —
`animation.h`'s own note that this project has no character/skeleton data format
yet) or how the cooker would read UsdSkel — only where the source lives and that
it stays USD, so today's mesh-only character content already has a home to cook
from.

The cooker keeps its existing one-argument-per-run shape
(`augusta-pack <scenario>`, ADR-0030). Addressing stays collision-free without a
new indirection layer (ADR-0018 already rejects that kind of layer): a
character's blobs are addressed under its name (e.g.
`soldier/Character/Visual`), and a map's own content by prim path relative to
its own stage root. The two meet only where a character's name is one of the
map's root prims, and the cooker refuses that scenario, so they never collide.
Client/server split follows ADR-0019's existing rule unchanged: mesh/texture
data is client-pack-only, hitbox geometry and the eye ship to both packs — the
server needs them for hit detection and for where a Shot leaves from, same as
level collision.

A scenario's packs carry exactly the characters its manifest names (ADR-0041),
not every character under `authoring/characters/`.

## Considered Options

**Every character under `authoring/characters/` packed into every scenario, with
no binding step**: rejected (ADR-0041). It saves a list to maintain, but every
scenario pack would ship the whole character library.

**Characters cooked into their own separate pack, loaded by the runtime
alongside the scenario's client/server pack**: rejected. The runtime
(`Pack::Load`/`ResolveMesh`, etc.) only ever resolves paths inside one
already-verified pack; multi-pack loading and cross-pack resolution would be a
real, currently-undesigned change to that runtime surface, for no benefit at
this scale over just merging everything at cook time.

**glTF as the character authoring format instead of USD**: rejected — reopens
exactly the "parallel import path" ADR-0016 rejected Assimp over.
USD-Fileformat-plugins already let a glTF/FBX source compose as a USD layer
without a second pipeline, so there's no format-support reason to leave USD for
characters specifically.
