# Explosions: a blast, Fragments on the ballistics, and a server-decided Dazzle

ADR-0045 made a grenade a Prop and gave an Explosion its radial impulse, but
left its damage to the weapons. The game gets five sources of Explosion: the
fragmentation grenade, the smoke grenade, the flashbang, the launched round
(grenade launcher and rocket) and the explosive Prop (ADR-0071). This ADR
decides what an Explosion does to players, how launched rounds fly, and who
decides a flashbang's effect. It takes effect in the change that brings the
first Explosion, which amends ADR-0002 (rounds that carry a charge), ADR-0038
(the Explosion and Dazzle messages), ADR-0039 (the Explosions' Parameters) and
ADR-0045 (an Explosion's damage) to match.

**An Explosion is the server's, at one place and Tick.** Damage resolves it in
the Tick it happens; its impulse joins the next Tick's Dynamics (ADR-0045). The
server tells every client reliably: its kind, where and on which Tick. Each
client draws its Effect (ADR-0073) and plays its `explosion` cue from that, and
derives everything else from the same seed. An Explosion never moves a living
player, for the reason a Prop does not (ADR-0045): being thrown by physics is
neither predictable nor fair. A Death from one throws the ragdoll away from its
centre.

**A fragmentation grenade hurts by blast and by Fragments.** The blast is short:
damage falling off with distance to every player that a raycast from the centre
reaches past the Map, as for the impulse, counted against the torso. Past it, a
fixed number of Fragments, a Parameter, leave the centre in directions drawn
from a seed of the Explosion's place and Tick, and fly as rounds on the
ballistics (ADR-0002), with penetration and ricochet by Surface material
(ADR-0065), a Body part where they enter a player, and damage by the energy
left. They are judged against players where the server has them now, not in a
Shooter's delay (ADR-0044): nobody aimed them. A client flies the same Fragments
from the same seed and draws only their impacts, never their tracers.

**A launched round flies on the ballistics.** A grenade launcher's grenade and a
rocket are rounds, announced by the Shot message and drawn by every client from
it, like a bullet. They explode on their first impact past an arming distance (a
Parameter); short of it, they strike as an inert round. A rocket burns out
inside its tube, as an AT4's does, and flies as a free round like the grenade:
neither adds thrust to the integration, and both join the golden trajectories
(NFR-03). Neither needs a Prop or a new message.

**Kills follow the thrower.** An Explosion's kills go to whoever threw or fired
what set it off, the thrower included, whose Death names it as its own killer;
the Rules decide what that counts for. An explosive Prop's go to the last player
who damaged it within ADR-0045's window, else to the world, and a chain hands
its credit from Explosion to Explosion.

**A grenade can be cooked.** Pulling the pin starts the fuse; the Command
carries the Tick it is pulled on. A grenade held past its fuse explodes in the
hand.

**The server decides a Dazzle.** A flashbang's Explosion gives each player a
Dazzle from its distance, the line of sight from the centre to its Eye past the
Map and Props, and how far its last Command's view faces away. The server sends
each player its own reliably; the client turns it into a white-out (the whole
view in VR), muffled sound and ringing that fade over a time the Dazzle sets. A
client can always ignore it, but the rule and the values are the server's and
part of the Match capture (ADR-0050).

**A smoke grenade only announces.** Its Explosion does no damage and no impulse;
where and when it went off is all a client needs to grow its Smoke screen
(ADR-0072).

## Consequences

- **A thrown grenade is drawn early on its thrower's client**: a cosmetic
  stand-in thrown from the same Command, merged into the interpolated Prop when
  it arrives, since a Prop is never predicted (ADR-0045).
- **Fragments raise a Tick's ballistic work**: their number per Explosion and
  the bound on surfaces per round (ADR-0065) keep it bounded.

## Considered Options

- **Blast damage alone**: rejected. A door would shelter what Fragments go
  through, and every wound would be the torso's.
- **Fragments alone**: rejected. Standing on a grenade would depend on how its
  Fragments scatter.
- **Fragments in the thrower's Shooter's delay**: rejected. Nobody aimed them,
  and a thrower's ping would decide who they hit.
- **Launched rounds as Props**: rejected. A round that explodes on impact never
  bounces, and the ballistics already fly it deterministically for free.
- **A Dazzle computed by each client**: rejected. A changed client would never
  be dazzled, and the Match capture could not show it.
- **Knockback on living players**: rejected, as ADR-0045 rejects Props that push
  players.
