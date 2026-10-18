# VR client: a Play mode whose Tracked pose is the player's body

A player can play a Match on a PC VR headset as well as on a desktop, in the
same Matches. In VR the player's head, hands and body are tracked, and they are
what the player sees from, aims with and is hit on, not a camera mode over the
desktop game. This ADR decides how a VR player's body reaches the server, what
the server trusts of it, and how the client gets it.

**Two Play modes, mixed.** A client plays Desktop or VR, declares it in its Join
request and keeps it for the Match. Both join the same Lobby and the same
Matches: with 2 to 8 players, separate VR Matches would rarely fill. The Roster
shows each player's Play mode and the Match capture records it. No rule
compensates one mode against the other until captures show one is needed.

**PC VR through OpenXR, on both platforms.** The client talks to the headset
only through OpenXR, so any PC VR runtime (SteamVR, Meta's through Link, Monado)
serves it; a standalone headset joins through its PC link, never as a client of
its own. Windows is the reference platform and Linux runs the same code
(NFR-04).

**The headset gives the head and hands, cameras give the body.** OpenXR tracks
the head and both hands with low latency, which is what aim needs. External RGB
cameras on the player's PC give the torso, hips and legs, through an existing
pose estimation library behind an interface of the client's own, so the source
can be replaced; writing pose estimation is not this project's learning goal.
The cameras' space is aligned with OpenXR's continuously from the head and
wrists both see, with no calibration pose. Camera images never leave the client:
they are not sent, captured, logged or kept.

**The Tracked pose is rotations, never lengths.** With each Command a VR client
sends its Tracked pose: its Skeleton's joint rotations and its head's offset
from its body's root, in the body's space. Limb lengths are its Character's
(ADR-0061), never the player's, so a tall player and a short one have the same
Hitboxes and nobody is hit on a body smaller than the Character's.

**The server bounds the Tracked pose.** It clamps each joint to limits authored
with the Character's Skeleton and cooked into both packs, and to a maximum
angular speed, and the head's offset to a radius around the capsule. A joint the
cameras lost is marked by the client: the server keeps its last valid rotation
for a short window, then brings it to the Rest pose, so a covered camera never
yields a missing or impossible limb. The bounded pose is what the Posing phase
poses the Hitboxes from (ADR-0061).

**Desktop players are posed by their Clips.** A Desktop player's pose is
evaluated from its Character's Clips by its replicated state (ADR-0066), and a
VR player's is its bounded Tracked pose; from there Hitboxes, the weapon and its
Muzzle (ADR-0064) are placed by one path whatever the Play mode, and a Desktop
Command still carries only its view.

**Replicated and captured.** Every VR player's bounded Tracked pose travels in
the Authoritative State update, its rotations as the smallest three quaternion
components, within ADR-0061's joint cap, and is part of the Match capture, so
other players see the body, and a Replay and a Reenactment reproduce its hits.
Its exact layout, and the Play mode's in the Join request, join ADR-0038's table
when they land.

**Moving through the Map is the Movement's.** Walking in place, detected from
the tracked body's cadence and stride, becomes the Command's movement direction
and sprint; the direction is the hips', so the player can look around while
walking. The capsule is still moved only by Movement, with stamina, and walking
in the room moves the head, not the capsule, within a lean's radius, past which
the view fades. There is no teleport. The stance is read from the tracked body
with hysteresis and sent as the desired stance, which the server holds to
US-04's rules: prone means lying down. The body's yaw follows the head's past a
dead zone and is sent as today's.

**The local body is drawn from tracking.** The client draws its own head, hands,
body and weapon from each frame's Tracked pose, outside Prediction and
Reconciliation, which keep predicting the capsule only. Local fire feedback
leaves the local Muzzle; the outcome stays the server's (ADR-0044). Spatial
audio listens from the tracked head (ADR-0010), and the controllers' haptics are
local feedback that nothing on the server sees.

**Rendering.** In VR, OpenXR picks the GPU and creates the Vulkan device
(`XR_KHR_vulkan_enable2`), which NVRHI then wraps (ADR-0059); both eyes are
drawn in one pass with multiview (`VK_KHR_multiview`), at the headset's own
refresh rate. The view's field of view is never changed in VR, and a death fades
to the Spectator's view instead of tying the view to the player's ragdoll.

**Tested without a headset.** A Tracked pose source that plays back a recording
stands behind the same interface as the cameras and OpenXR, so the server's
bounds, the replication and the capture are tested in CI from recorded poses,
including poses that try to cheat.

## Consequences

- **ADR-0061 changes**: a VR player's Hitbox pose follows from its replicated
  Tracked pose, and a Desktop player's from its Clips (ADR-0066).
- **The Authoritative State update grows** by about 100 to 150 bytes per VR
  player.
- **The client gains a dependency on the player's hardware**: a headset, its
  OpenXR runtime and cameras. A client with none of them plays Desktop.

## Considered Options

- **VR as a screen only** (head-tracked camera, mouse aim): rejected. Aim stays
  the mouse's, and the view still has to be split from the aim.
- **VR-only Matches**: rejected. Two populations of 2 to 8 players rarely fill a
  Match.
- **Standalone headsets as a client**: rejected. An Android client is a third
  toolchain and GPU budget.
- **Thumbstick locomotion**: rejected for the tracked body; walking in place
  keeps movement physical while Movement keeps its rules.
- **The capsule following the head in the room**: rejected. It puts the body's
  position in the client's hands and lets walking pass through walls.
- **The player's own limb lengths**: rejected. Hitboxes would differ between
  players and could not be held to the Character's.
- **The headset's own body tracking only** (OpenXR body tracking extensions):
  rejected. It estimates the legs and is not offered by every runtime.
- **Body tracking on a desktop**: rejected. A third Play mode multiplies the
  bounds and the fairness to decide.
