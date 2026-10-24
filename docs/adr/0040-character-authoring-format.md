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
This follows GLOSSARY.md's own line between the two: "Map" is the static space a
match is played in — collision, spawn points, hitboxes — and a player character
is an ECS entity, not scene content, so it doesn't belong inside the map's stage
alongside the level.

A character has a **skeleton** (ADR-0061), authored as a UsdSkel `Skeleton`
prim, the `Skeleton` child of `Character`: its joints, each named by its path
within the skeleton (e.g. `Root/Pelvis/Spine`), with their rest transforms
relative to their parents. Its root joints sit at the character's root, the
feet, facing -Z, so joints, hitboxes and the `Eye` share one space. The cooker
writes it as a skeleton blob at `<character name>/Character/Skeleton` in both
packs, every joint, whether or not one carries a hitbox, each as its name, its
parent and its rest transform relative to that parent, in the stage's corrected
space. It refuses a character without one, a skeleton of more than 128 joints
(the pack format's cap), one UsdSkel could not pose (a joint before its parent,
two joints of one name, a rest transform missing), and one whose root joint is
not at the character's root, each naming the character's prim, or its
skeleton's.

A character is hittable through its **hitboxes**, authored in its own stage
under `Character`: each is geometry (a `UsdGeomMesh`, `Cube` or `Capsule`,
usually a guide so it is never drawn) marked `augusta:hitbox`, and names the
body part it stands for in `augusta:bodyPart`: `head`, `torso` or `limb`. A body
part may have several (two arms and two legs are four `limb` hitboxes). Each is
authored under the joint it moves with, as UsdSkel binds any prim rigidly to one
joint: `SkelBindingAPI`, its `skel:joints` naming that joint and a constant
`primvars:skel:jointIndices` of `[0]`, with `Character` the `SkelRoot` binding
the skeleton. They are placed relative to the feet, standing, on a character
that faces -Z, where a view of yaw 0 looks. The skeleton's root carries the
stance and the facing (ADR-0061): the server poses it lowered with the stance as
the client lowers the eye, scaling the vertical by the stance's height, and
turned about the vertical axis by the yaw its body faces (ADR-0038), and every
joint and the hitboxes bound to it move with the root, so no character authors
hitboxes per stance or per facing. Every hit on a player resolves to a body part
(US-11), so the cooker refuses a hitbox without a body part or with another
name, and a character without at least one hitbox for each of the three; and
every hitbox moves with a joint, so it refuses a hitbox authored under no joint,
or under one its skeleton lacks, naming the hitbox's prim. Each hitbox is a blob
at its own prim's path under the character (e.g.
`soldier/Character/HeadHitbox`): its body part as one byte, its joint's name as
a string, then its geometry as a mesh blob, composed into its joint's rest
space, so placing it is its joint's transform times its own, with no rest pose
to undo. A map's hitbox has the same blob, naming no joint. Both packs carry
them, and the server resolves every hitbox under a character's name at startup,
refusing to run on a character it could not judge a hit on.

Characters are authored in USD, same toolchain and rationale as ADR-0016:
Composer for assembly, Adobe's USD-Fileformat-plugins composing any glTF/FBX/OBJ
source as a USD layer rather than a second import path. Mesh geometry follows
the exact `UsdGeomMesh` → meshoptimizer path ADR-0016/ADR-0031 already define.
The skeleton is UsdSkel in the same stage, read by the cooker through the same
`pxr` it walks the stage with; its runtime form, and the poses evaluated from
it, are ADR-0061's. Skinning a mesh to it is not cooked yet: its bindings will
be an optional section of the mesh blob, in the client pack alone, once
something reads them.

The cooker keeps its existing one-argument-per-run shape
(`augusta-pack <scenario>`, ADR-0030). Addressing stays collision-free without a
new indirection layer (ADR-0018 already rejects that kind of layer): a
character's blobs are addressed under its name (e.g.
`soldier/Character/Visual`), and a map's own content by prim path relative to
its own stage root. The two meet only where a character's name is one of the
map's root prims, and the cooker refuses that scenario, so they never collide.
Client/server split follows ADR-0019's existing rule unchanged: mesh/texture
data is client-pack-only, hitbox geometry, the skeleton and the eye ship to both
packs — the server needs them for hit detection and for where a Shot leaves
from, same as level collision.

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
