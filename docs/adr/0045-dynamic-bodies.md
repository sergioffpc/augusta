# Dynamic Bodies: Server-Authoritative Props, Client-Only Cosmetics, Fixed-Step Simulate

A World used to move bodies only through PhysX character controllers and never
simulate its scene (ADR-0002). The game needs bodies that fall, tumble and are
pushed: objects that block movement and bullets, fall on players, and are thrown
by shots and explosions; ragdolls; debris. This ADR decides who owns each kind
of dynamic body, where `PxScene::simulate()` runs on each side, and how dynamic
bodies meet movement, ballistics and damage.

**Two kinds of dynamic body.** A **Prop** affects gameplay: it blocks players
and bullets, can hurt a player, and is moved by players, bullets and explosions.
The Authoritative server alone simulates it, gives it an Entity ID like any
other body, and replicates it; a client never simulates a Prop, it moves it as a
kinematic body to the pose interpolated from the Authoritative State. A Prop is
either placed in the Map (authored in Composer, ADR-0015, and cooked into both
packs, ADR-0031) or created during a Match, such as a thrown grenade. A
**Cosmetic body** never affects gameplay: ragdolls and debris (spent casings,
chips off a wall, bottles off a shelf). It exists only on one client, is never
sent, and may differ between clients.

**Particles are not physics.** Sparks, dust, smoke and blood belong to the
renderer, as a compute particle system that at most collides against the depth
buffer. PhysX's PBD particles need CUDA, which the headless Linux server lacks,
and would couple PhysX to the GPU the renderer uses.

**Where simulate runs.** On the server, SimulationWorld's Dynamics phase
(ADR-0023), after Movement, runs one `simulate()` per Tick with the tick's fixed
step. Impulses from a tick's shots and explosions are queued and applied in the
next tick's Dynamics; a tick's delay is not perceptible. On the client, the
World is simulated only for Cosmetic bodies and Props' kinematic poses, so the
Dynamics phase is PresentationWorld's (ADR-0024), not PredictionWorld's:
Reconciliation replays Movement several times per tick, and a simulate inside
that replay would advance debris more than once. Presentation runs per frame,
but its Dynamics steps fixed: one `simulate()` of one tick's step for each tick
the PredictionWorld advanced since the last frame, at most a few per frame, the
excess dropped after a hitch. Cosmetic bodies are drawn interpolated between the
last two steps with the Interpolation phase's own fraction, so they move in step
with the players around them. PhysX never sees a variable step on either side.

**One World per process, split by collision filters.** The client keeps a single
World. Cosmetic bodies collide with the Map, with Props and with controllers,
but controllers and gameplay queries never see them. The PredictionWorld moves
only its own player's controller; the PresentationWorld sets every other pose
and runs `simulate()`. A client's predicted movement therefore collides with
Props at the last interpolated pose, in the past; when a player pushes a Prop
the prediction is corrected by Reconciliation, since predicting the Prop too
would need physics deterministic between client and server, which PhysX does not
give. Two Worlds per client were rejected: they duplicate the Map's collision in
memory and must keep their Props in sync.

**Players push Props; Props never move players.** When a controller's move hits
a Prop, it applies an impulse from the player's speed, bounded by the Prop's
mass: a light box slides, a container does not. A Prop only blocks and hurts a
controller, never displaces it, since a `PxController` is not pushed by dynamic
bodies and being thrown by physics is neither predictable nor fair.

**Prop damage is mechanism reporting, not physics deciding.** Dynamics reports
each Prop-player contact above a threshold, with its impulse. The Damage phase
turns it into damage from the Parameters (ADR-0039), and credits the kill to the
last player who pushed, shot or blew up that Prop within a window the Parameters
also set; past it, the kill is the world's.

**Bullets and Props.** A bullet's segment is tested against Props where they are
now, through physics, next to the Map (ADR-0044 judges players in the Shooter's
delay). Props are almost always at rest, and a pose history for them would test
poses the scene no longer holds and push a Prop where it no longer is. A bullet
that hits a Prop stops there, and pushes it with an impulse each weapon's data
sets, with the bullet's real momentum only as a reference, since a few grams at
800 m/s barely move a crate. Penetration by material and thickness, for Props
and Map alike, is a separate decision.

**Grenades are Props, bullets are not.** A grenade bounces, rolls and waits for
its fuse, which is what PhysX's rigid bodies do well: it is a Prop created when
thrown, with continuous collision detection, and it explodes when its fuse runs
out. Bullets keep their own ballistics (ADR-0002). An explosion applies, in
Dynamics, a radial impulse falling off with distance to every body in its radius
that a raycast from its centre reaches past the Map (`RaycastMap`, which ignores
Props and controllers), so a wall shelters what is behind it. Its damage is the
weapons' decision, not this one.

**Ragdolls start from a replicated death.** A ragdoll's bodies and joints are
authored per Character in Composer (ADR-0015) and cooked into the client pack
only (ADR-0031). A player's death is told reliably to every client (the Death
message, ADR-0038), with the body, the direction of the killing blow and the
Body part it struck, so each client throws its ragdoll the right way; the Hit
confirmation, which only the shooter receives (ADR-0044), cannot. The Body part
stands in for a bone: a Character's skeleton is not designed, and its bones
would name no more than the hitboxes the server judges by. Ragdolls exist only
after death: hit reactions while alive are animation.

**Replication.** Props travel in the Authoritative State as a list apart from
the players' bodies, which carry stance and stamina that Props lack but no
rotation, which Props need: entity ID, position on ADR-0038's grid, rotation as
the smallest three quaternion components, and linear velocity for interpolation.
Only awake Props are sent. When a Prop falls asleep the server sends its final
pose reliably, since a Prop at rest is no longer sent and a lost final pose
would leave the client wrong for good; spawning and removing a Prop during a
Match are reliable too. The protocol caps how many Props are awake at once,
beyond which those furthest from every player are put to sleep; the cap's value
comes from a benchmark (ADR-0013). The exact layouts join ADR-0038's table when
they land.

**Threads.** A World simulates on the thread that runs its tick, with a
`PxDefaultCpuDispatcher` of no worker threads and no GPU dynamics. A server node
runs several Matches, and worker threads are added only if a benchmark shows a
Match needs them.

## Consequences

- **A World now simulates.** Controllers' actors reach their kinematic targets
  at each simulate, which changes what ADR-0002 says a raycast sees; shots still
  never test controllers (ADR-0044).
- **SimulationWorld and PresentationWorld each gain a Dynamics phase**
  (ADR-0023, ADR-0024), and the client's World is written by two pipelines, each
  owning its own bodies.
- **Dynamic physics has no golden tests.** PhysX is not deterministic across
  Windows and Linux, so unlike the ballistic trajectories (NFR-03) it is
  verified by properties: a dropped box comes to rest on the floor, a shot
  pushes a Prop away from the shooter, a push does not move a heavy Prop.
- **Hitting a moving Prop depends on ping**, unlike hitting a player: it is
  judged where the Prop is now, not where the shooter saw it.

## Considered Options

- **Client-predicted Props**: rejected - it needs the same physics result on
  client and server, which PhysX gives across neither platforms nor builds.
- **Props tested in the Shooter's delay**: rejected - a pose history per Prop,
  queries against poses the scene no longer holds, and an impulse applied where
  the Prop no longer is, for bodies that are almost always at rest.
- **Props that push players**: rejected - a controller would have to add a
  Prop's velocity to its own move, and being thrown by physics can neither be
  predicted nor made fair.
- **Server-authoritative ragdolls**: rejected - a dead body is neither a target
  nor cover, and replicating every bone of every corpse costs bandwidth and
  server time for nothing gameplay needs.
- **Client Dynamics in PredictionWorld**: rejected - Reconciliation's replay
  would advance Cosmetic bodies once per replayed tick, and Props' poses come
  from interpolation, which is Presentation's.
- **Client Dynamics at the frame's variable step**: rejected - PhysX is not
  stable with a varying step, and a second clock would drift from the ticks that
  drive interpolation.
- **Grenades on the bullets' own ballistics**: rejected - bouncing and rolling
  are rigid-body dynamics, which PhysX already does.
- **Explosion impulses that pass through the Map**: rejected - a grenade behind
  a wall would throw the whole room.
