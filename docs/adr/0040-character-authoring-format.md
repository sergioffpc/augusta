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
where a player's line of sight starts (US-07): a round leaves the rifle's
Muzzle, never the eye, and the server zeroes it to cross the line of sight from
the shooter's eye, lowered for its stance the same way, along its view at the
rifle's zero range (ADR-0064), so a shot goes where the sight looks. The cooker
writes that point, in the character's root space, as an eye blob at
`<character name>/Character/Eye` in both packs, and refuses a character stage
without one; the client reads the eye of the character it plays and keeps the
camera there, following its body, and the server reads the eye of every
character of the scenario at startup, refusing to run on one it could not zero a
round for. A character is never itself the argument the cooker (`augusta-pack`)
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

A character names its **Clips** (ADR-0066) by role under `Character/Clips`: each
a UsdSkel `SkelAnimation` prim at its role's path, usually a reference to the
Clip's own file under `authoring/animations/`, so characters whose skeletons
share its joints share it. A stance's roles are under the stance's name
(`standing/idle`, `crouching/forward`, `prone/aim_up`, `standing/ads`...) and
the rest at the top (`air`, and the Cosmetic layers' `reload`, `recoil`,
`flinch` and `land`). A Clip is authored in its character's units and axes,
moving as tools make it, and a locomotion Clip names its Foot contacts in
`augusta:footContacts`, a token time-sampled at each contact's time code with
the foot that lands, `left` or `right` (ADR-0062). The `Clips` prim authors the
transition duration of each pair of stances in seconds, either way:
`augusta:transitionDuration:standing_crouching`, `standing_prone` and
`crouching_prone`. The cooker converts the skeleton and each Clip into
ozz-animation's archives, joint for joint in the stage's corrected space, and
for each locomotion Clip measures its stride length, how far its root moves
along the ground from its first time code to its last, then removes that
progress, and where it started, from its root, so it plays in place with its
sway and height kept. It refuses, naming the Clip's prim under the character's:
a required role with no Clip, a Clip that is not a `SkelAnimation` or whose
joints are not exactly its skeleton's, a locomotion Clip without exactly one
left and one right Foot contact within it or whose root does not move along the
ground, an idle with a Foot contact or whose root does, and a pair of stances
without a positive transition duration.

A character holds the rifle by its **weapon socket** (ADR-0066), the
`WeaponSocket` child of `Character`: bound rigidly to its right hand's joint as
a hitbox is, placed where the rifle's Grip goes, pointing the way the rifle does
along its -Z, and naming its left hand's joint in `augusta:leftHand`, the hand
that reaches the rifle's Fore-end, whose parent and grandparent are the elbow
and shoulder it bends. The cooker writes it at
`<character name>/Character/WeaponSocket` in both packs, its frame in its
joint's rest space, and refuses a character without one, one bound to no joint
or to one its skeleton lacks, and a left hand missing, unknown or without a
parent and a grandparent, naming the socket's prim or the character's.

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
facing (ADR-0061): the server poses it where the body stands, turned about the
vertical axis by the yaw its body faces (ADR-0038), never scaled by the stance;
how low the body is in a stance is that stance's Clips' (ADR-0066), which move
its joints. Every joint and the hitboxes bound to it move with the root and the
Clips, so no character authors hitboxes per stance or per facing. Every hit on a
player resolves to a body part (US-11), so the cooker refuses a hitbox without a
body part or with another name, and a character without at least one hitbox for
each of the three; and every hitbox moves with a joint, so it refuses a hitbox
authored under no joint, or under one its skeleton lacks, naming the hitbox's
prim. Each hitbox is a blob at its own prim's path under the character (e.g.
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
data is client-pack-only, hitbox geometry, the skeleton, the Clips the Hitbox
pose uses and the eye ship to both packs — the server needs them for hit
detection, for where a Shot leaves from and for the line of sight it is zeroed
onto, same as level collision.

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
