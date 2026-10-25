# Character animation

A player's Hitbox pose follows from replicated state (ADR-0061), but nothing yet
says what a Character's Clips are, how locomotion, stance, aim and the weapon
become a pose, what the client draws on top of it, how the body is skinned, or
how a dead body becomes a ragdoll. This ADR decides them. It takes effect in the
change that brings Clips, which amends ADR-0038 (the new replicated fields) and
ADR-0040 (the stance no longer scales the Skeleton) to match, and brings
footsteps with it (ADR-0062).

**ozz-animation on both sides.** Sampling, blending, additive layers and the
two-bone IK are ozz-animation's, in the shared Animation module (ADR-0061). The
cooker converts each Skeleton and Clip from UsdSkel into ozz-animation's
archives, so the runtime never reads USD.

**One file per Clip.** A Clip is a `UsdSkelAnimation` in a USD file of its own
under `authoring/animations/`, and a Character names the Clips it uses by role
(`walk_forward`, `crouch_idle`...) in its own stage. A Clip may serve several
Characters whose Skeletons share its Joints; the cooker refuses one whose Joints
do not match its Character's Skeleton, naming both. Clips the Hitbox pose uses
ship in both packs, Cosmetic layers' Clips in the client's alone (ADR-0019).

**No root motion.** The capsule is moved by Movement alone, with its stamina and
collision rules. Locomotion Clips are authored moving; the cooker measures each
one's stride length from its root's translation and removes it, so the Clip
plays in place. A player's Stride, one cycle of two steps, advances by the
distance its body moves along the ground divided by the stride length of its
stance's blend space at its velocity, never by time, so the feet do not slide at
any speed and the Stride follows from the movement alone. Standing still, it
holds where it was. Movement advances it, in SimulationWorld and in the owner's
PredictionWorld alike, from the stance and velocity the last tick left, in the
space the tick's yaw faces, each tick's advance rounded to a 256th of the cycle,
the byte it travels as; so a client loads its own Character's Clips to predict
it, and Reconciliation corrects it as it corrects the position.

**A blend space per stance, no state machine.** Each stance has a 2D blend space
over the body's velocity in its own space: idle, forward, backward, the sides,
optional diagonals, and sprint when standing. Its Clips are synchronised by the
Stride, normalised between each Clip's two Foot contacts, which serve as sync
markers. The pose is a function of the stance, the velocity, the Stride and
whether the body is on the ground; there is no animation state with a time of
its own, which ADR-0061 rules out.

**Stance changes blend by a replicated progress.** A change of stance blends the
previous stance's pose into the new one's over a duration the Character authors
for each pair of stances. The previous stance and the progress are gameplay
state, replicated and captured; Movement advances the progress by time over that
duration, never by a clock of its own. A change back to the stance a change
still under way left turns that change round where it has got to; a change to a
third stance starts afresh from the stance it leaves. The collider still changes
height at once, as today, and Movement's rules do not change; the Hitboxes are
where the body is drawn, halfway through when the change is.

**In the air.** A body off the ground (`kGrounded`, ADR-0062) takes its `air`
Clip, sampled by its vertical velocity from rising to falling, in place of the
blend space. It switches on the tick the bit changes, with no crossfade, since a
crossfade needs a time of its own.

**Aim and grip.** An Aim offset, additive poses authored at the view's lowest,
level and highest pitch, bends the body by its pitch; the body turns by its yaw
as today, with no turning in place. Each of them and the stance's ADS grip is
authored as a whole pose, and the level one is the reference the others add
their difference from: the lowest or highest by how far the pitch is toward it,
the ADS grip whole while ADS is set, each added onto the locomotion pose. The
weapon hangs from a **weapon socket** on the right hand, so its Muzzle follows
the pose (ADR-0064), and the left hand reaches its **Fore-end** by two-bone IK.
The Character authors the socket as its `WeaponSocket` prim, bound rigidly to
its right hand's Joint as a Hitbox is, its frame where the rifle's Grip goes,
naming its left hand's Joint in `augusta:leftHand`; that Joint's parent and
grandparent are the elbow and the shoulder the IK turns. The rifle authors its
Fore-end as a marker beside its Grip. The cooker refuses a Character without a
socket, one bound to no Joint of its Skeleton, and a left hand missing, unknown
or without an elbow and a shoulder, and a rifle without a Fore-end, naming them.
The elbow bends the way the pose bent it, toward the side of the line from the
shoulder to the hand it was on, so the Clips decide where it points; a Fore-end
out of the arm's reach leaves the arm straight toward it. The grip, ADS and the
left hand's IK are Hitbox pose and run on both sides, so a player's view pitch
and ADS are replicated.

**Desktop and VR.** A Desktop player's pose is evaluated from its Clips; a VR
player's is its bounded Tracked pose (ADR-0063). From there both take one path:
Hitboxes, the weapon and its Muzzle.

**Required Clips.** The cooker refuses a Character missing one, naming it:
standing and crouching `idle`, `forward`, `backward`, `left` and `right`,
standing `sprint`, prone `idle`, `forward` and `backward`; each stance's Aim
offset and ADS grip; `air`; and a transition duration for each pair of stances.
A locomotion Clip has exactly two Foot contacts, one left and one right, and its
root moves; an `idle` has neither. Cosmetic layers' Clips (`reload`, `recoil`,
`flinch`, `land`) are optional, and a missing one leaves its layer still.

**Cosmetic layers.** The client draws on top of the Hitbox pose, never moving a
Hitbox: foot placement, by two-bone IK against the ground; a flinch, when it
learns a player was hit; the landing; the reload and the recoil. The reload
moves forearms that carry Hitboxes, but replicating every player's reload for
centimetres of forearm is not worth it.

**Drawing.** PresentationWorld's Animation phase evaluates the pose of every
player in view and of the local player, from the interpolated and predicted
states, with no level of detail at 8 players, and adds the Cosmetic layers. The
local player sees its own whole body from the posed head, with the head and neck
hidden from its camera, so it sees what the others see. A VR client draws its
own body from each frame's Tracked pose instead (ADR-0063).

**Skinning.** Linear blend skinning with at most 4 influences per vertex, which
the cooker keeps strongest and renormalises, in a compute pass that writes the
skinned vertices every later pass reads, shadows and both of a VR client's eyes
included. A Skeleton has at most 128 Joints, enough for hands; the Hitbox
history still keeps only the Joints a Shot needs (ADR-0061).

**From pose to ragdoll.** On a player's Death message (ADR-0045) the client
builds its ragdoll in the pose it draws that frame, each body's velocity taken
from the last two frames' poses, and applies the killing blow's impulse at the
Joint of the struck Hitbox. The switch is immediate. A body stays until the
Match ends; past a cap of ragdolls set in the client's config (ADR-0034), the
oldest freezes into a static mesh.

## Consequences

- **The Authoritative State grows**: each player's Stride, view pitch, ADS,
  previous stance and stance progress, about 4 bytes.
- **A Replay's per-player view message goes**: the pitch and ADS it carried are
  in the Authoritative State.
- **The stance no longer scales the Skeleton**: ADR-0040's lowering by stance
  height gives way to each stance's Clips.
- **Footsteps can land**: the Stride and the Foot contacts they need exist.

## Considered Options

- **Our own pose evaluator**: rejected. Sampling, compression, blending and IK
  are a large body of code that is not this project's learning goal.
- **Clips inside the Character's stage**: rejected. Characters sharing a
  Skeleton could not share their locomotion.
- **Root motion moving the capsule**: rejected. It takes movement out of
  Movement's rules and stamina, and out of prediction.
- **A state machine with timed crossfades**: rejected. It is animation state the
  server and clients would have to agree on.
- **Procedural aim IK**: rejected. It needs no content, but leaves an author no
  control over how a body bends.
- **Separate first-person arms**: rejected. They look better but lie about where
  the body is, and differ from the VR path.
- **Dual quaternion skinning**: rejected. It deforms shoulders better at more
  cost and with weaker tool support.
- **Reload as Hitbox pose**: rejected. It would replicate every player's reload
  for a few centimetres of forearm.
