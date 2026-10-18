# Footsteps

A player's footsteps are heard when its stride passes a Foot contact of its
locomotion clip, on the surface the foot lands on. This decision takes effect
with the clip format, which ADR-0061 and the Skeleton's spec leave open: the
change that brings locomotion clips also brings footsteps, and amends ADR-0031,
ADR-0032, ADR-0038 and ADR-0041 to match.

## Decisions

- **Detected from the stride, not from distance or the pose.** Where a stride
  is, is gameplay state replicated like any other (ADR-0061). A footstep plays
  on the frame a player's stride crosses one of its clip's Foot contacts, for
  the local player from the Prediction State and for every other player from the
  interpolated Authoritative State. No pose is evaluated for it, so it is the
  same for the local and remote players, and a Replay hears the same footsteps.
- **Foot contacts are authored.** A locomotion clip names its Foot contacts in
  `augusta:footContacts`: each one a time in the clip and the foot (left or
  right) that lands. The cooker carries them into the clip and derives none.
- **Stance and gait.** Walking, sprinting and crouching each step at a gain the
  scenario sets; prone steps not at all. A footstep is the first part of US-04's
  stealth.
- **The surface is the collider's Surface material.** The client raycasts down
  through physics against the Map's collision under the body, and the collider
  it hits names its Surface material with `augusta:material` (ADR-0065), the
  same attribute and catalogue as Acoustic geometry. On a collider the attribute
  is optional: one without it steps as `generic`. One vocabulary of materials,
  so concrete is concrete both to a wall's reflections and to the foot.
- **Sounds per surface, in every cue's shape.** `footstep` and `landing` are
  cues like any other, so their sounds take the one shape every cue's sound has
  in the scenario manifest (ADR-0041): variants played at random without
  repeats, and a gain. What they add is a choice by the parameters their
  detection hands over: the Surface material underfoot for both, and the gait
  for `footstep`, each choice its own variants and gain. Both require a
  `generic` choice, the fallback for a material with no sounds of its own, so no
  step is ever silent. The choice by parameter is general, not footsteps' own: a
  gunshot choosing by weapon takes the same form.
- **Landing.** A body that comes back to the ground after a fall or a jump plays
  the `landing` cue of the surface it lands on. Whether a body is on the ground
  is replicated as a `kGrounded` bit in the body state's flags (ADR-0038): no
  step is heard in the air, and a remote landing is exact rather than guessed
  from vertical speed.
- **Where a footstep is heard.** Another player's from the body's feet, one
  position for both feet. The local player's own at the listener, the same way
  as its own gunshot: heard with the reflections of where it stands, with no
  direction and no occlusion.

## Considered Options

- **A step every stride length of distance moved**: rejected. It needs nothing
  that does not exist yet, but it would be replaced the day clips arrive, and a
  step out of time with the feet drawn is worse than none.
- **Foot contacts derived at cook time from each foot joint's lowest point**:
  rejected. No contact list to maintain, but a clip whose foot never quite
  reaches its lowest point at landing would step out of time, with nothing for
  an author to correct it with.
- **Game policy choosing a footstep's sound in Lua**: rejected. Neither client
  world runs policy (ADR-0022, ADR-0024); the server deciding would be a message
  per step and a round trip late on the local player's own. Which sound a
  surface makes is content, carried by the manifest.
- **A separate `augusta:surface` vocabulary on colliders**: rejected for one
  Surface material shared with acoustics and ballistics (ADR-0065).
