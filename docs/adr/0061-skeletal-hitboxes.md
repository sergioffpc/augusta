# Skeletal hitboxes

A player's Hitboxes move with its skeleton: an arm reaching out is hit where it
is drawn. Client and server evaluate the same Hitbox pose from the same state,
so a Shot is judged against the body the shooter saw (ADR-0044).

Today's Hitboxes are rigid in the character's space: the server lowers them with
the stance and turns them by the body's yaw (ADR-0040), and the Hitbox history
keeps position, stance and facing (ADR-0044). That holds until characters have a
skeleton, which ADR-0040 leaves open. This decision takes effect with the
skeleton format: the change that brings it amends ADR-0019, ADR-0023, ADR-0040
and ADR-0044 to match.

## Decisions

- **An Animation module, shared.** Skeletons, clips and pose evaluation are one
  module scoped `SHARED`, below Presentation, depending on Physics and Gameplay
  only. Skinning is the Renderer's, on the GPU, from the pose's joint matrices;
  the server links no rendering code.
- **Hitbox pose and cosmetic layers.** The Hitbox pose is locomotion, stance,
  aim and how the weapon is held. Foot placement, a flinch, breathing and the
  face are cosmetic layers the client draws on top of it. Anything that moves a
  Hitbox is Hitbox pose, never a cosmetic layer.
- **A pure function of replicated state.** A player's Hitbox pose follows from
  its state in the Authoritative State and the tick, with no animation state of
  its own. What the pose needs to remember, such as how far a change of stance
  has gone or where a stride is, is gameplay state, replicated like any other,
  so neither side rebuilds history to agree and a Replay reproduces the pose.
- **Hitboxes belong to joints.** Each Hitbox is authored under a joint of the
  character's UsdSkel skeleton and moves rigidly with it. The cooker refuses a
  Hitbox under no joint. ADR-0040's lowering by stance and turning by yaw give
  way to the pose.
- **A Posing phase.** SimulationWorld evaluates every player's Hitbox pose in a
  phase of its own between WeaponHandling and Ballistics, so HitDetection judges
  this tick's Shots against it. Commit records it in the Hitbox history.
- **What the Hitbox history keeps.** Per tick, the transforms of the joints that
  carry a Hitbox. Lag compensation interpolates them joint by joint between the
  two ticks the Seen time falls between: position linearly, rotation along the
  shorter arc.
- **Packs.** The skeleton and the clips the Hitbox pose uses ship in both packs;
  cosmetic clips and skinning data in the client's alone (ADR-0019).
- **No pose in PredictionWorld.** It predicts the local player, whom no one
  shoots locally. SimulationWorld evaluates poses for Hit detection, and
  PresentationWorld's Animation phase for every visible player, first-person
  arms included.
- **No bit-exact agreement across platforms.** The Windows client and the Linux
  server may differ in the last bits of a pose. The server's is authoritative,
  the client's only draws, and tests comparing them use a tolerance.

## Considered Options

- **Rigid Hitboxes, as today**: rejected. A shot at an arm or a head that the
  pose has moved out of the rigid volume misses on the server while it hit on
  the shooter's screen.
- **Animation on the client only**: rejected with the rigid Hitboxes; the server
  needs the pose to judge a hit.
- **Re-evaluating the pose when lag compensating**: rejected. Each Shot would
  evaluate every candidate's pose at the Shooter's delay, where storing the
  joints costs a few kilobytes per player.
- **Replicating the pose**: rejected. Joint transforms for every player every
  tick cost bandwidth that the pose's inputs, already replicated, make
  unnecessary.
