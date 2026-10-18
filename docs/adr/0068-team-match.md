# Team match

Every Match is last player standing today: no player has a side, the Winner is
one Session, and a Spectator watches anyone. Milsim is played between sides, and
Voice chat (ADR-0069) has no one to talk to without them. This ADR makes every
Match a team match and drops the free-for-all. It takes effect in the change
that brings Teams, which amends ADR-0022 (the hooks), ADR-0038 (the messages),
ADR-0041 (the manifest), ADR-0042 and ADR-0040 (the Character's Uniforms),
ADR-0043 (Match start), ADR-0050 (the Match capture) and ADR-0052 (the Agents'
view) to match.

**Two Teams, always.** Every Match is played between two Teams, so 2 to 8
players make 1v1 to 4v4, and an odd number makes uneven Teams. A Player count of
1, kept for development and tests (ADR-0043), puts its player on Team 1 and
leaves Team 2 empty. There is no free-for-all mode.

**A Team is the mechanism's, its rules are policy's.** Each player has a Team
for a whole Match: the engine knows it, sends it, captures it and routes Voice
chat and the Spectator's view by it. Game policy decides who is on which Team
and when one has won. Rules, messages and captures name Teams 1 and 2.

**The scenario names its Teams.** The manifest names its two Teams, each with a
name and a Uniform (`portugal` wearing `pt_army`, `russia` wearing `ru_army`).
The names are content, for logs and a future interface; nothing in the rules or
the protocol reads them.

**Assignment by the rules.** A new hook, `assign_teams(match)`, runs at Match
start, before `assign_spawns`. Its view is the Match start's players; it returns
each player's Team. The answer is refused, as `assign_spawns`'s is, when it
names a player twice or leaves one out, names a Team other than 1 or 2, or
leaves a Team empty with two players or more; then, as when the hook is
undefined, players alternate between the Teams in Session order.

**The Winner is a Team.** `on_tick` returns `{winner = <Team>}` or
`{draw = true}`; a Session is no longer a Winner. Every player on the winning
Team wins, alive or dead. A Draw is both Teams' last players dying on the same
tick, a Match of one ending in its player's death, or the last player leaving. A
Team emptied by disconnections is the rules' to judge, since a hook sees who is
still in the Match; the example rules, `last_team_standing.lua`, give the Match
to the other Team. The mechanism still ends a Match by itself only when its last
player leaves.

**Friendly fire is full.** Damage and ballistics ignore Teams: a round hurts a
teammate as it hurts an enemy, with no penalty for a team kill, and the
mechanism gains no case for it.

**Uniforms tell friend from enemy.** There are no names, no markers over heads
and no interface, so a teammate is known by what it wears. Each Character
authors a USD variant set, `uniform`, with one variant per Uniform it can wear,
each binding the Looks of its prims (ADR-0067). A player wears its Team's
Uniform on whichever Character it chose, which it chose before its Team was
known (ADR-0042). The cooker refuses a Character of the scenario without a
variant for each of the scenario's Uniforms, naming both, and cooks only the
variants the scenario uses. Until Looks land, a variant may change
`displayColor`.

**Spawn points have a Team.** Each Spawn point is authored with `augusta:team`,
1 or 2. `assign_spawns`'s view gains each player's Team and each Spawn point's;
when the hook is undefined or its answer is refused, each player takes its own
Team's Spawn points in order, starting over after the last. The cooker refuses a
Map with a Spawn point without a Team or with none for one Team, naming it.

**The Spectator stays with its Team.** A dead player watches from a living
teammate's eye, in Session order; only once its Team has no one alive, if the
rules keep the Match going, does it watch any living player. A Spectator never
scouts the enemy for its Team.

**Sent and captured.** The Match start message carries each player's Team and
the Match end names the winning Team. The Match capture records the Teams, so a
Replay and a Reenactment play the same sides, and an Agent's view holds its own
Team and every other player's.

## Consequences

- **The free-for-all is gone**: `last_man_standing.lua` becomes
  `last_team_standing.lua`, and a scenario can no longer be won by one player.
- **Every Character is authored with its Uniforms**, and every Map with Spawn
  points for both Teams.
- **US-14 is met as written**: its "all players on one side are eliminated"
  finally has sides.

## Considered Options

- **Keeping the free-for-all beside team matches**: rejected. Voice chat and the
  Spectator would need two behaviours, for a mode milsim does not play.
- **Teams as Game policy alone**: rejected. Voice chat and the Spectator are
  mechanisms and must know who is on which side.
- **Players choosing their Team in the client config**: rejected. With an
  automatic Lobby and no interface, nothing would stop three against none.
- **Characters per Team**: rejected. A player picks its Character before its
  Team is known; a Uniform lets any Character serve either side.
- **Markers over teammates' heads**: rejected. Less realistic than a Uniform,
  and an interface the engine does not have.
- **Friendly fire off**: rejected. It is a special case in damage for less
  realism.
- **Fixed sides named in code**: rejected. The scenario decides who fights whom.
