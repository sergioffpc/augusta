# Shot Lag Compensation and Replication: The Shooter's Time, Server-Confirmed Hits, Announced Shots

A client sees other players about 100 ms in the past (the Interpolation delay)
plus half the RTT, and its fire Command takes another half RTT to reach the
server. Without compensation, a shot the shooter saw hit a moving target misses
on the server, and aim depends on ping rather than skill. This ADR decides how
the server judges a shot against what the shooter saw, how long a bullet keeps
that judgement, how the shooter learns of a hit, and how every client learns of
a shot. It extends the message catalogue of ADR-0038 and changes ADR-0024's
split of ballistics between client and server.

**Lag compensation favors the shooter, capped at 250 ms.** A fire Command
carries its Seen time, what the shooter was seeing when it fired: the tick of the Authoritative
State update being shown and the interpolation fraction between it and the next
one (0 to 1). The server keeps, for every player, the hitboxes of each of its
recent ticks, as that tick's Authoritative State reported them, enough ticks to
cover the cap at its tick rate (15 at 60 Hz). It tests the shot against the
hitboxes interpolated between those two ticks at that fraction: position and
facing interpolated, the facing along the shorter arc, and the stance of the
nearer tick, which is how a client shows a body between two updates. The time between
that Seen time and the tick the server takes the Command in is the Shooter's delay. A
delay beyond 250 ms is clamped to 250 ms, not refused: the shot is judged
against the oldest Seen time the cap allows. The cap bounds how far into the past a
high-latency player can hit, which is the cost the victim pays (being shot
after reaching cover). The Seen time is only what a client says, so the server holds
it within what it can have been shown: a fraction outside 0 to 1 is the nearer
of the two, and a Seen time newer than the last Authoritative State the server
emitted is that State, so a delay is never less than a tick.

**A bullet lives in the shooter's time for its whole flight.** At 800 m/s a
bullet takes several ticks to reach its target. The server fixes the Shooter's
delay once, when the shot is fired. Every tick it advances the bullet (ADR-0002)
and tests that tick's segment against the hitboxes as they were that fixed delay
ago; the Map is static and Props are judged where they are now (ADR-0045), so
neither needs a history. A lead the shooter gave a moving
target, judged on its own screen, then hits. Because the delay is fixed and
capped, every tick of a flight finds the hitboxes it needs within the same
bounded history.

**Hit feedback is confirmed by the server, not predicted.** When a shot hits a
player, the server sends the shooter a Hit confirmation: the target, the body
part and the damage. It is reliable, since its loss would leave the shooter
never told of a hit that happened (ADR-0038's rule). The hit marker and sound
appear only when it arrives, after the flight time plus half an RTT, and a
false hit marker is never shown. The client keeps predicting only its own fire
feedback (muzzle flash, sound, recoil, ammo), never a bullet's outcome
(ADR-0024).

**The server announces every Shot to everyone, reliably.** A Shot message (the
shooter, by its Entity ID; the server tick; the origin and the direction) goes
reliably to every client, the shooter's included: a gunshot's sound is gameplay
information and must not be lost. With GameNetworkingSockets (ADR-0003), a lost
reliable message does not delay unreliable ones, so the Authoritative State
updates are not held back behind it.

**Clients draw a Shot's trajectory as a visual only.** Each client's
PresentationWorld computes the announced bullet's trajectory with the same
ballistics math the server uses and the ballistics values of the Parameters
(ADR-0039), to draw the tracer and the impact on the Map. Hits on players
(blood, hit reactions) are drawn only from the server's hit messages, never from
that local trajectory, so a client's drawing can differ from the server's
outcome without ever showing a hit that did not happen. `augusta_ballistics`'
trajectory math is therefore shared by client and server; deciding a bullet's
outcome (HitDetection, Damage) stays the server's alone.

**On the wire.** The Shot, the Hit confirmation and the Command's Seen time
(its tick and fraction) follow ADR-0038: the smallest types, quantized numbers on its
grids, and `Wire` types converted only at `server::Host` and
`harness::Session`. ADR-0038's table has their layout. Every Command carries
its Seen time, not only one with fire pressed: whether a Command fires a round is
the server's to decide (its WeaponHandling keeps the fire rate), not the
client's to know.

## Consequences

- **Player hits are not tested against the physics scene's current bodies.**
  A shot is tested against the hitbox history at the Shooter's delay, and
  against the Map and Props through physics. ADR-0002's note on raycasting a moved body
  therefore does not decide player hit detection.
- **`augusta_ballistics` leaves the server-only modules.** Its trajectory
  integration is used by the client's presentation as well; its hit testing
  and the outcome it reports stay server-side.
- **A shooter's hit marker lags its shot** by the flight time plus half an RTT.
  That is the price of never showing one that is taken back.

## Considered Options

- **No lag compensation**: rejected - a shot is judged where targets are when
  the command arrives, not where the shooter saw them, so aim at any moving
  target depends on ping rather than skill.
- **Refusing a shot whose delay exceeds the cap**: rejected - the shooter's
  trigger would do nothing; clamping still fires the shot, just judged against
  the oldest Seen time the cap allows.
- **Rewinding only the first tick of a flight, then testing against the
  present**: rejected - it switches timelines mid-flight, so long shots at
  moving targets miss inconsistently.
- **A delay that shrinks during the flight**: rejected - it has no physical
  meaning, and the hitboxes a tick is tested against would drift between the
  shooter's Seen time and the present.
- **Client-side hit prediction**: rejected - it needs a second ballistics and
  hitbox simulation on the client and shows hit markers that are later taken
  back.
- **Announcing only impacts**: rejected - the tracer could be drawn only after
  the flight, backwards, and with no drop.
- **A "firing" bit in the Authoritative State only**: rejected - it gives no
  tracer and no impacts, and an unreliable bit can be lost with its gunshot.
