# Skeletal hitboxes

A player's Hitboxes move with its skeleton: an arm reaching out is hit where it
is drawn. Client and server evaluate the same Hitbox pose from the same state,
so a Shot is judged against the body the shooter saw (ADR-0044).

Rigid Hitboxes in the character's space, scaled down with the stance and turned
by the body's yaw, with a Hitbox history of position, stance and facing, hold
only until characters have a skeleton. This decision resolves the skeleton
format; ADR-0019, ADR-0023, ADR-0040 and ADR-0044 are amended to match.

## Decisions

- **An Animation module, shared.** Skeletons, clips and pose evaluation are one
  module scoped `SHARED`, below Presentation, depending on Physics and Gameplay
  only. Skinning is the Renderer's, on the GPU, from the pose's joint matrices;
  the server links no rendering code. The module takes the name `animation`: the
  presentation module's stub of that name — an empty `Pose`, an `Engine` whose
  every method returns nothing, and a `kAnimation` phase that calls none of it —
  is deleted rather than kept beside it, so one term names one thing.
- **Hitbox pose and Cosmetic layers.** The Hitbox pose is locomotion, stance,
  aim and how the weapon is held: its grip and ADS. Foot placement, a flinch,
  the landing, the reload, the recoil, breathing and the face are Cosmetic
  layers the client draws on top of it (ADR-0066). A Cosmetic layer never moves
  a Hitbox, so whatever does is Hitbox pose.
- **A pure function of replicated state.** A player's Hitbox pose follows from
  its state in the Authoritative State and the tick, with no animation state of
  its own. What the pose needs to remember, such as how far a change of stance
  has gone or where a stride is, is gameplay state, replicated like any other,
  so neither side rebuilds history to agree and a Replay reproduces the pose. A
  VR player's state includes its bounded Tracked pose, which is replicated and
  captured; a Desktop player is posed from its Clips (ADR-0066), and from there
  both Play modes take one path (ADR-0063).
- **Authored as UsdSkel.** A character's skeleton is authored as `UsdSkel`
  inside the character's own stage, under `Character` (ADR-0040), and its Clips
  as `UsdSkel` too (ADR-0066), not as an imported glTF skin and not in a format
  of this engine's own: one import path, the same reason ADR-0016 rejected
  Assimp.
- **One skeleton per character, no retargeting.** Each character authors its own
  skeleton. Nothing at runtime maps one skeleton's joints onto another's; a Clip
  serves several characters only when their skeletons share its joints
  (ADR-0066).
- **A joint is named, not numbered.** The cooked format names a joint by its
  path within the skeleton — the form UsdSkel already uses, and unique where a
  bare token is not, two `Shoulder` joints under `Left` and `Right`. Names are
  resolved once, when the character is loaded, into indices that everything
  afterwards works on; a name that does not resolve makes the server refuse to
  run on that character, as a hitbox without a body part already does
  (ADR-0040).
- **The same joint list in both packs.** Both packs carry the identical list,
  including joints that carry no Hitbox. Pruning the server's would give "this
  character's skeleton" two answers depending on the side, which is the question
  asked most often when a pose differs between client and server; a skeleton is
  small, and what is large — skinning data and Cosmetic layers' Clips — is
  already client-only (ADR-0019).
- **Local transforms.** A joint carries its transform relative to its parent,
  plus its parent, as UsdSkel authors it and as a clip changes it. World
  transforms are composed at load.
- **The skeleton's root is the character's root.** The root joint sits at the
  origin of the character's stage — the feet, facing -Z — so joints, Hitboxes
  and the `Eye` share one space. The cooker refuses a skeleton rooted elsewhere.
- **At most 128 joints.** Enough for skinning hands, and refused at cook time.
  The Hitbox history's cost is ticks × joints × a transform per player, and
  without a cap it is unknown until someone authors a face. The cap is the pack
  format's, not the protocol's: a pose is never replicated.
- **Hitboxes belong to joints.** Each Hitbox is authored under a joint of the
  character's UsdSkel skeleton and moves rigidly with it. The cooker refuses a
  Hitbox under no joint, and composes its geometry into its joint's space, so
  placing it at runtime is the joint's world transform times the Hitbox's own —
  no rest pose to invert per tick.
- **A skeleton is mandatory.** The cooker refuses a character without one, as it
  already refuses one without an `Eye` or without a Hitbox for each body part.
  There is no rigid fallback and so no second hit-detection path.
- **The `Eye` stays a marker until Clips.** With the rest pose alone, the camera
  sits at the authored `Eye` prim, lowered by stance, as ADR-0040 has it — not
  at a joint of the skeleton — and rounds leave the rifle's Muzzle (ADR-0064).
  Once Clips move a head, the local player sees from the posed head instead
  (ADR-0066).
- **A Posing phase.** SimulationWorld evaluates every player's Hitbox pose in a
  phase of its own between WeaponHandling and Ballistics, so HitDetection judges
  this tick's Shots against it. Commit records it in the Hitbox history.
- **What the Hitbox history keeps.** Per tick, where the Hitbox pose put the
  skeleton's root — its position, facing and stance — and, apart from it, the
  transforms relative to their parents of the joints that carry a Hitbox and the
  ancestors needed to compose them: not the whole skeleton, and no scale, which
  no pose changes. Lag compensation interpolates between the two ticks the Seen
  time falls between: the root's position linearly, its facing along the shorter
  arc and its stance the nearer tick's; each joint's position linearly and its
  rotation along the shorter arc; then composes the joints onto the root from
  the root down (ADR-0044). The root is kept apart because it is the body's
  position and facing, which interpolate as a client shows them.
- **Culling stays conservative.** The reach that discards a candidate before its
  triangles are tested (ADR-0044) is taken from the pose being judged: each
  Hitbox's furthest point from its joint, measured when the character is loaded,
  added to how far that pose holds the joint from the root, plus a margin. It
  holds whatever a Clip does with the joints, with nothing to measure over every
  Clip.
- **The root carries the position and the facing alone.** The skeleton's root is
  where the body's position puts it, turned about the vertical by its yaw
  (ADR-0040); the stance does not scale it. How low a body is in a stance is
  that stance's Clips' (ADR-0066). A character without Clips, which only a test
  builds, stands in its rest pose in every stance.
- **No replicated state is added.** In the rest pose there is nothing to
  remember: position, yaw and stance are already in the Authoritative State. The
  protocol, the Authoritative State update and the Match capture are untouched
  until Clips bring memory of their own (ADR-0066).
- **No pose on the client at first.** The module is `SHARED` so that it cannot
  depend on anything client-only, but in the first change SimulationWorld is the
  one consumer that poses players. The other is a Development build's client
  (ADR-0074), which places a character's Hitboxes in its root space by the rest
  pose once, when the character loads, for the Hitboxes Debug overlay to place
  by each player's pose as the root: a loader, not a World, posing no player.
  PresentationWorld's Animation phase stays as it is until Clips and skinning
  arrive, because without skinning nothing the client draws moves with a pose.
- **Skinning comes with the Clips.** Drawing a posed character is ADR-0066's,
  not part of the first change. The renderer rebuilds every remote player's
  vertices on the CPU each frame, rotated by yaw and translated, with no
  per-instance transform, which GPU skinning needs first. Until then the server
  judges hits against a posed body while the client draws an unposed one — which
  is what happens today, and the server is authoritative either way.
- **Where skinning data will live.** Skin bindings — the joints and weights of
  each vertex (ADR-0066) — are an optional section of the mesh blob, written
  into the client's pack alone (ADR-0019); the server judges Shots against
  Hitbox triangles, never against the mesh. The section is written when skinning
  has a reader: this engine already cooks character textures and a character
  collider that nothing resolves, and a third is not wanted.
- **Packs.** The skeleton and the Clips the Hitbox pose uses ship in both packs;
  Cosmetic layers' Clips and skinning data in the client's alone (ADR-0019). A
  new entry type and a mandatory skeleton make a pack cooked before this change
  invalid after it, so the pack format's version rises and the runtime refuses
  the older one with the error it already has, rather than loading it and
  failing at the first Shot.
- **No pose in PredictionWorld.** It predicts the local player, whom no one
  shoots locally. SimulationWorld evaluates poses for Hit detection, and
  PresentationWorld's Animation phase for every visible player and the local
  player's own whole body (ADR-0066). A VR client draws its own body from each
  frame's Tracked pose instead (ADR-0063).
- **No bit-exact agreement across platforms.** The Windows client and the Linux
  server may differ in the last bits of a pose. The server's is authoritative,
  the client's only draws, and tests comparing them use a tolerance.
- **A client poses the interpolated state.** Between two Authoritative State
  updates a client interpolates every input of the pose and poses the result:
  position, velocity, pitch and the progress of a change of stance both updates
  are of linearly, the facing along the shorter arc, the Stride the shorter way
  round its cycle, and the stance, a change of stance otherwise, the ground and
  ADS from the nearer update. The server judges between two ticks by
  interpolating their posed Joints (ADR-0044) instead. On a tick the two agree
  but for the wire's grids; between two, within a few centimetres where a Clip
  turns a Joint back, and not while a discrete input changes between them.

## Considered Options

- **Rigid Hitboxes, as today**: rejected. A shot at an arm or a head that the
  pose has moved out of the rigid volume misses on the server while it hit on
  the shooter's screen.
- **Animation on the client only**: rejected with the rigid Hitboxes; the server
  needs the pose to judge a hit.
- **A joint numbered by its index in the pack**: rejected. It costs no string
  resolution at load, but a joint reordered in authoring silently rebinds every
  Hitbox bound to it, and an index means nothing to someone reading a pack.
- **Pruning the server pack's skeleton to the joints that carry a Hitbox**:
  rejected. It saves little and makes the same joint a different thing on each
  side.
- **Re-evaluating the pose when lag compensating**: rejected. Each Shot would
  evaluate every candidate's pose at the Shooter's delay, where storing the
  joints costs a few kilobytes per player.
- **Replicating the pose**: rejected. Joint transforms for every player every
  tick cost bandwidth that the pose's inputs, already replicated, make
  unnecessary. A VR player's Tracked pose is one of those inputs, not the pose:
  joint rotations alone, bounded by the server, from which the Hitboxes are
  still posed.
- **Hitbox geometry kept in the character's root space with a joint reference**:
  rejected. Every Hitbox would have its joint's rest transform undone before the
  current pose is applied, every tick, to express what the cooker can compose
  once.
- **An optional skeleton, so packs cooked before it keep loading**: rejected.
  Two hit-detection paths to maintain, and a pack that loads but cannot judge a
  shot.
- **Clips in the same change as the format**: rejected. The format, the binding
  and the history are verifiable against today's behaviour with the rest pose
  alone; clips bring authored content, replicated pose memory and a blending
  model, each of which is a decision of its own.
