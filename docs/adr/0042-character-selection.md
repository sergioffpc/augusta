# Character Selection: Chosen in the Client Config, Validated at Join, Replicated as an Index

A scenario can now compose more than one character (ADR-0041), and a character
is gameplay, not just appearance: its `Collider` ships in the server pack
(ADR-0040), so which character a player plays affects the simulation. This ADR
decides where a player's choice comes from, who has the final word on it, and
how every client learns every other player's character. It extends the message
catalogue of ADR-0038.

**The choice comes from the client config.** `augustac.yaml` (ADR-0034) gains a
required `character` key naming a character by its path relative to
`authoring/`, the same address the manifest's `characters` list uses (ADR-0041),
e.g. `character: characters/player`. There is no lobby and no selection screen
(CONTEXT.md: a Match has no pre-match waiting state), so the config is the only
place a player can state a choice today. A future UI only changes who fills in
the value, not the protocol beneath it.

**The server validates it at join and has the final word.** The Join request
carries the character path next to the engine version, as a string: it is sent
once per session, so its size doesn't matter, and it keeps the client from
depending on the manifest's order to name its own choice. The server admits the
player only if the path is one of its scenario's characters, and otherwise
refuses with a new Join refused reason, *unknown character*. The order of checks
becomes version, then character, then match full, so a client that can never
play here is not told "full". The character is fixed for the life of the
session: no message changes it after admission.

**Every other message names a character by index.** Both packs of a scenario are
cooked from the same manifest, so the cooker records its `characters` list, in
manifest order, in both the client and the server pack. A character's
**character index** is its 1-based position in that list, so a zeroed byte is
never valid, following ADR-0038's enum rule. That caps a scenario at 255
characters. Join accepted tells the joining player its own index, each Roster
entry carries one, and so does every player in the Authoritative State.
A client drops an update whose index is outside its own pack's list, as it drops
any message that does not decode.

**The index rides in every Authoritative State, not in a join event.** Players
who join mid-match first reach a client through the Authoritative State, which
is unreliable (ADR-0038). Putting the index in that update makes each update
self-sufficient: a client never sees a player whose character it doesn't know,
and no ordering between a reliable and an unreliable channel has to be handled.
The cost is at most 8 bytes per tick.

**The client draws each player as its character.** The client loads the visual
mesh of every character in its pack's list at startup, keyed by index, and the
renderer draws each remote player with the mesh its index names. This replaces
the single fixed `characters/player/Player/Visual` mesh drawn for everyone,
which stood in only while no selection existed.

## Considered Options

- **The server assigns characters (Game policy), with no client choice**:
  rejected, because a player should play what they picked. Assignment rules such
  as team-restricted characters can still arrive later as Game policy that
  refuses or narrows the requested choice, without changing where the request
  comes from.
- **The client requests and the server may override**: rejected for now as
  machinery with no rule to drive it yet. If policy ever needs to substitute a
  character, Join accepted already carries the player's own index, so the server
  could return a different one without a wire change.
- **A reliable "player joined" message carrying the character instead of a
  per-tick index**: rejected. The unreliable Authoritative State can arrive
  before it, so the client would have to hold or hide a player whose character
  it doesn't know yet. That is an ordering problem the per-tick byte avoids.
- **The character path as a string in every Authoritative State**: rejected,
  because it costs about 20–30 bytes per player per tick on the one channel
  whose size matters.
- **Fall back to the first character in the manifest on an unknown choice**:
  rejected, because the player would silently play something they didn't pick.
  A refusal with a reason matches how a version mismatch is already treated.
